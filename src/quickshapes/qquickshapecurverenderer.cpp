// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qquickshapecurverenderer_p.h"
#include "qquickshapecurverenderer_p_p.h"

#include <QtGui/qvector2d.h>
#include <QtGui/qvector4d.h>
#include <QtGui/private/qtriangulator_p.h>
#include <QtGui/private/qtriangulatingstroker_p.h>
#include <QtGui/private/qrhi_p.h>

#include <QtQuick/private/qsgcurvefillnode_p.h>
#include <QtQuick/private/qsgcurvestrokenode_p.h>
#include <QtQuick/private/qquadpath_p.h>
#include <QtQuick/private/qsgcurveprocessor_p.h>
#include <QtQuick/qsgmaterial.h>

#include <QThread>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(lcShapeCurveRenderer, "qt.shape.curverenderer");

namespace {

class QQuickShapeWireFrameMaterialShader : public QSGMaterialShader
{
public:
    QQuickShapeWireFrameMaterialShader()
    {
        setShaderFileName(VertexStage,
                          QStringLiteral(":/qt-project.org/shapes/shaders_ng/wireframe.vert.qsb"));
        setShaderFileName(FragmentStage,
                          QStringLiteral(":/qt-project.org/shapes/shaders_ng/wireframe.frag.qsb"));
    }

    bool updateUniformData(RenderState &state, QSGMaterial *, QSGMaterial *) override
    {
        bool changed = false;
        QByteArray *buf = state.uniformData();
        Q_ASSERT(buf->size() >= 64);

        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();

            memcpy(buf->data(), m.constData(), 64);
            changed = true;
        }

        return changed;
    }
};

class QQuickShapeWireFrameMaterial : public QSGMaterial
{
public:
    QQuickShapeWireFrameMaterial()
    {
        setFlag(Blending, true);
    }

    int compare(const QSGMaterial *other) const override
    {
        return (type() - other->type());
    }

protected:
    QSGMaterialType *type() const override
    {
        static QSGMaterialType t;
        return &t;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override
    {
        return new QQuickShapeWireFrameMaterialShader;
    }

};

class QQuickShapeWireFrameNode : public QSGCurveAbstractNode
{
public:
    struct WireFrameVertex
    {
        float x, y, u, v, w;
    };

    QQuickShapeWireFrameNode()
    {
        setFlag(OwnsGeometry, true);
        setGeometry(new QSGGeometry(attributes(), 0, 0));
        activateMaterial();
    }

    void setColor(QColor col) override
    {
        Q_UNUSED(col);
    }

    void activateMaterial()
    {
        m_material.reset(new QQuickShapeWireFrameMaterial);
        setMaterial(m_material.data());
    }

    static const QSGGeometry::AttributeSet &attributes()
    {
        static QSGGeometry::Attribute data[] = {
            QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
            QSGGeometry::Attribute::createWithAttributeType(1, 3, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
        };
        static QSGGeometry::AttributeSet attrs = { 2, sizeof(WireFrameVertex), data };
        return attrs;
    }

    void cookGeometry() override
    {
        // Intentionally empty
    }

protected:
    QScopedPointer<QQuickShapeWireFrameMaterial> m_material;
};
}


QQuickShapeCurveRenderer::~QQuickShapeCurveRenderer() { }

void QQuickShapeCurveRenderer::beginSync(int totalCount, bool *countChanged)
{
    if (countChanged != nullptr && totalCount != m_paths.size())
        *countChanged = true;
    m_paths.resize(totalCount);
}

void QQuickShapeCurveRenderer::setPath(int index, const QQuickPath *path)
{
    auto &pathData = m_paths[index];
    pathData.originalPath = path->path();
    pathData.m_dirty |= PathDirty;
}

void QQuickShapeCurveRenderer::setStrokeColor(int index, const QColor &color)
{
    auto &pathData = m_paths[index];
    const bool wasVisible = pathData.isStrokeVisible();
    pathData.pen.setColor(color);
    if (pathData.isStrokeVisible() != wasVisible)
        pathData.m_dirty |= StrokeDirty;
    else
        pathData.m_dirty |= UniformsDirty;
}

void QQuickShapeCurveRenderer::setStrokeWidth(int index, qreal w)
{
    auto &pathData = m_paths[index];
    if (w > 0) {
        pathData.validPenWidth = true;
        pathData.pen.setWidthF(w);
    } else {
        pathData.validPenWidth = false;
    }
    pathData.m_dirty |= StrokeDirty;
}

void QQuickShapeCurveRenderer::setFillColor(int index, const QColor &color)
{
    auto &pathData = m_paths[index];
    const bool wasVisible = pathData.isFillVisible();
    pathData.fillColor = color;
    if (pathData.isFillVisible() != wasVisible)
        pathData.m_dirty |= FillDirty;
    else
        pathData.m_dirty |= UniformsDirty;
}

void QQuickShapeCurveRenderer::setFillRule(int index, QQuickShapePath::FillRule fillRule)
{
    auto &pathData = m_paths[index];
    pathData.fillRule = Qt::FillRule(fillRule);
    pathData.m_dirty |= PathDirty;
}

void QQuickShapeCurveRenderer::setJoinStyle(int index,
                                          QQuickShapePath::JoinStyle joinStyle,
                                          int miterLimit)
{
    auto &pathData = m_paths[index];
    pathData.pen.setJoinStyle(Qt::PenJoinStyle(joinStyle));
    pathData.pen.setMiterLimit(miterLimit);
    pathData.m_dirty |= StrokeDirty;
}

void QQuickShapeCurveRenderer::setCapStyle(int index, QQuickShapePath::CapStyle capStyle)
{
    auto &pathData = m_paths[index];
    pathData.pen.setCapStyle(Qt::PenCapStyle(capStyle));
    pathData.m_dirty |= StrokeDirty;
}

void QQuickShapeCurveRenderer::setStrokeStyle(int index,
                                            QQuickShapePath::StrokeStyle strokeStyle,
                                            qreal dashOffset,
                                            const QVector<qreal> &dashPattern)
{
    auto &pathData = m_paths[index];
    pathData.pen.setStyle(Qt::PenStyle(strokeStyle));
    if (strokeStyle == QQuickShapePath::DashLine) {
        pathData.pen.setDashPattern(dashPattern);
        pathData.pen.setDashOffset(dashOffset);
    }
    pathData.m_dirty |= StrokeDirty;
}

void QQuickShapeCurveRenderer::setFillGradient(int index, QQuickShapeGradient *gradient)
{
    PathData &pd(m_paths[index]);
    pd.gradientType = QGradient::NoGradient;
    if (QQuickShapeLinearGradient *g  = qobject_cast<QQuickShapeLinearGradient *>(gradient)) {
        pd.gradientType = QGradient::LinearGradient;
        pd.gradient.stops = gradient->gradientStops();
        pd.gradient.spread = QGradient::Spread(gradient->spread());
        pd.gradient.a = QPointF(g->x1(), g->y1());
        pd.gradient.b = QPointF(g->x2(), g->y2());
    } else if (QQuickShapeRadialGradient *g = qobject_cast<QQuickShapeRadialGradient *>(gradient)) {
        pd.gradientType = QGradient::RadialGradient;
        pd.gradient.a = QPointF(g->centerX(), g->centerY());
        pd.gradient.b = QPointF(g->focalX(), g->focalY());
        pd.gradient.v0 = g->centerRadius();
        pd.gradient.v1 = g->focalRadius();
    } else if (QQuickShapeConicalGradient *g = qobject_cast<QQuickShapeConicalGradient *>(gradient)) {
        pd.gradientType = QGradient::ConicalGradient;
        pd.gradient.a = QPointF(g->centerX(), g->centerY());
        pd.gradient.v0 = g->angle();
    } else
    if (gradient != nullptr) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            qCWarning(lcShapeCurveRenderer) << "Unsupported gradient fill";
        }
    }

    if (pd.gradientType != QGradient::NoGradient) {
        pd.gradient.stops = gradient->gradientStops();
        pd.gradient.spread = QGradient::Spread(gradient->spread());
    }

    pd.m_dirty |= FillDirty;
}

void QQuickShapeCurveRenderer::setAsyncCallback(void (*callback)(void *), void *data)
{
    qCWarning(lcShapeCurveRenderer) << "Asynchronous creation not supported by CurveRenderer";
    Q_UNUSED(callback);
    Q_UNUSED(data);
}

void QQuickShapeCurveRenderer::endSync(bool async)
{
    Q_UNUSED(async);
}

void QQuickShapeCurveRenderer::updateNode()
{
    if (!m_rootNode)
        return;
    static const bool doOverlapSolving = !qEnvironmentVariableIntValue("QT_QUICKSHAPES_DISABLE_OVERLAP_SOLVER");
    static const bool useTriangulatingStroker = qEnvironmentVariableIntValue("QT_QUICKSHAPES_TRIANGULATING_STROKER");
    static const bool simplifyPath = qEnvironmentVariableIntValue("QT_QUICKSHAPES_SIMPLIFY_PATHS");

    for (PathData &pathData : m_paths) {
        int dirtyFlags = pathData.m_dirty;

        if (dirtyFlags & PathDirty) {
            if (simplifyPath)
                pathData.path = QQuadPath::fromPainterPath(pathData.originalPath.simplified());
            else
                pathData.path = QQuadPath::fromPainterPath(pathData.originalPath);
            pathData.path.setFillRule(pathData.fillRule);
            pathData.fillPath = {};
            dirtyFlags |= (FillDirty | StrokeDirty);
        }

        if (dirtyFlags & FillDirty) {
            deleteAndClear(&pathData.fillNodes);
            deleteAndClear(&pathData.fillDebugNodes);
            if (pathData.isFillVisible()) {
                if (pathData.fillPath.isEmpty()) {
                    pathData.fillPath = pathData.path.subPathsClosed();
                    pathData.fillPath.addCurvatureData();
                    if (doOverlapSolving)
                        QSGCurveProcessor::solveOverlaps(pathData.fillPath);
                }
                pathData.fillNodes = addFillNodes(pathData, &pathData.fillDebugNodes);
                dirtyFlags |= StrokeDirty;
            }
        }

        if (dirtyFlags & StrokeDirty) {
            deleteAndClear(&pathData.strokeNodes);
            deleteAndClear(&pathData.strokeDebugNodes);
            if (pathData.isStrokeVisible()) {
                const QPen &pen = pathData.pen;
                if (pen.style() == Qt::SolidLine)
                    pathData.strokePath = pathData.path;
                else
                    pathData.strokePath = pathData.path.dashed(pen.widthF(), pen.dashPattern(), pen.dashOffset());

                if (useTriangulatingStroker)
                    pathData.strokeNodes = addTriangulatingStrokerNodes(pathData, &pathData.strokeDebugNodes);
                else
                    pathData.strokeNodes = addCurveStrokeNodes(pathData, &pathData.strokeDebugNodes);
            }
        }

        if (dirtyFlags & UniformsDirty) {
            if (!(dirtyFlags & FillDirty)) {
                for (auto &pathNode : std::as_const(pathData.fillNodes))
                    pathNode->setColor(pathData.fillColor);
            }
            if (!(dirtyFlags & StrokeDirty)) {
                for (auto &strokeNode : std::as_const(pathData.strokeNodes))
                    strokeNode->setColor(pathData.pen.color());
            }
        }

        pathData.m_dirty &= ~(PathDirty | FillDirty | StrokeDirty | UniformsDirty);
    }
}

QQuickShapeCurveRenderer::NodeList QQuickShapeCurveRenderer::addFillNodes(const PathData &pathData,
                                                                  NodeList *debugNodes)
{
    auto *node = new QSGCurveFillNode;
    node->setGradientType(pathData.gradientType);

    NodeList ret;
    const QColor &color = pathData.fillColor;
    QPainterPath internalHull;
    internalHull.setFillRule(pathData.fillPath.fillRule());

    bool visualizeDebug = debugVisualization() & DebugCurves;
    const float dbg = visualizeDebug  ? 0.5f : 0.0f;
    node->setDebug(dbg);

    QVector<QQuickShapeWireFrameNode::WireFrameVertex> wfVertices;

    QSGCurveProcessor::processFill(pathData.fillPath,
                                   pathData.fillRule,
                                   [&wfVertices, &node](const std::array<QVector2D, 3> &v,
                                                        const std::array<QVector2D, 3> &n,
                                                        QSGCurveProcessor::uvForPointCallback uvForPoint)
                                   {
                                       node->appendTriangle(v, n, uvForPoint);

                                       wfVertices.append({v.at(0).x(), v.at(0).y(), 1.0f, 0.0f, 0.0f}); // 0
                                       wfVertices.append({v.at(1).x(), v.at(1).y(), 0.0f, 1.0f, 0.0f}); // 1
                                       wfVertices.append({v.at(2).x(), v.at(2).y(), 0.0f, 0.0f, 1.0f}); // 2
                                   });

    QVector<quint32> indices = node->uncookedIndexes();
    if (indices.size() > 0) {
        node->setColor(color);
        node->setFillGradient(pathData.gradient);

        node->cookGeometry();
        m_rootNode->appendChildNode(node);
        ret.append(node);
    }

    const bool wireFrame = debugVisualization() & DebugWireframe;
    if (wireFrame) {
        QQuickShapeWireFrameNode *wfNode = new QQuickShapeWireFrameNode;
        QSGGeometry *wfg = new QSGGeometry(QQuickShapeWireFrameNode::attributes(),
                                           wfVertices.size(),
                                           indices.size(),
                                           QSGGeometry::UnsignedIntType);
        wfNode->setGeometry(wfg);

        wfg->setDrawingMode(QSGGeometry::DrawTriangles);
        memcpy(wfg->indexData(),
               indices.data(),
               indices.size() * wfg->sizeOfIndex());
        memcpy(wfg->vertexData(),
               wfVertices.data(),
               wfg->vertexCount() * wfg->sizeOfVertex());

        m_rootNode->appendChildNode(wfNode);
        debugNodes->append(wfNode);
    }

    return ret;
}

QQuickShapeCurveRenderer::NodeList QQuickShapeCurveRenderer::addTriangulatingStrokerNodes(const PathData &pathData, NodeList *debugNodes)
{
    NodeList ret;
    const QColor &color = pathData.pen.color();

    QVector<QQuickShapeWireFrameNode::WireFrameVertex> wfVertices;

    QTriangulatingStroker stroker;
    const auto painterPath = pathData.strokePath.toPainterPath();
    const QVectorPath &vp = qtVectorPathForPath(painterPath);
    QPen pen = pathData.pen;
    stroker.process(vp, pen, {}, {});

    auto *node = new QSGCurveFillNode;
    node->setGradientType(pathData.gradientType);

    auto uvForPoint = [](QVector2D v1, QVector2D v2, QVector2D p)
    {
        double divisor = v1.x() * v2.y() - v2.x() * v1.y();

        float u = (p.x() * v2.y() - p.y() * v2.x()) / divisor;
        float v = (p.y() * v1.x() - p.x() * v1.y()) / divisor;

        return QVector2D(u, v);
    };

    // Find uv coordinates for the point p, for a quadratic curve from p0 to p2 with control point p1
    // also works for a line from p0 to p2, where p1 is on the inside of the path relative to the line
    auto curveUv = [uvForPoint](QVector2D p0, QVector2D p1, QVector2D p2, QVector2D p)
    {
        QVector2D v1 = 2 * (p1 - p0);
        QVector2D v2 = p2 - v1 - p0;
        return uvForPoint(v1, v2, p - p0);
    };

    auto findPointOtherSide = [](const QVector2D &startPoint, const QVector2D &endPoint, const QVector2D &referencePoint){

        QVector2D baseLine = endPoint - startPoint;
        QVector2D insideVector = referencePoint - startPoint;
        QVector2D normal = QVector2D(-baseLine.y(), baseLine.x()); // TODO: limit size of triangle

        bool swap = QVector2D::dotProduct(insideVector, normal) < 0;

        return swap ? startPoint + normal : startPoint - normal;
    };

    static bool disableExtraTriangles = qEnvironmentVariableIntValue("QT_QUICKSHAPES_WIP_DISABLE_EXTRA_STROKE_TRIANGLES");

    auto addStrokeTriangle = [&](const QVector2D &p1, const QVector2D &p2, const QVector2D &p3, bool){
        if (p1 == p2 || p2 == p3) {
            return;
        }

        auto uvForPoint = [&p1, &p2, &p3, curveUv](QVector2D p) {
            auto uv = curveUv(p1, p2, p3, p);
            return QVector3D(uv.x(), uv.y(), 0.0f); // Line
        };

        node->appendTriangle(p1, p2, p3, uvForPoint);


        wfVertices.append({p1.x(), p1.y(), 1.0f, 0.0f, 0.0f}); // 0
        wfVertices.append({p2.x(), p2.y(), 0.0f, 0.1f, 0.0f}); // 1
        wfVertices.append({p3.x(), p3.y(), 0.0f, 0.0f, 1.0f}); // 2

        if (!disableExtraTriangles) {
            // Add a triangle on the outer side of the line to get some more AA
            // The new point replaces p2 (currentVertex+1)
            QVector2D op = findPointOtherSide(p1, p3, p2);
            node->appendTriangle(p1, op, p3, uvForPoint);

            wfVertices.append({p1.x(), p1.y(), 1.0f, 0.0f, 0.0f});
            wfVertices.append({op.x(), op.y(), 0.0f, 1.0f, 0.0f}); // replacing p2
            wfVertices.append({p3.x(), p3.y(), 0.0f, 0.0f, 1.0f});
        }
    };

    const int vertCount = stroker.vertexCount() / 2;
    const float *verts = stroker.vertices();
    for (int i = 0; i < vertCount - 2; ++i) {
        QVector2D p[3];
        for (int j = 0; j < 3; ++j) {
            p[j] = QVector2D(verts[(i+j)*2], verts[(i+j)*2 + 1]);
        }
        bool isOdd = i % 2;
        addStrokeTriangle(p[0], p[1], p[2], isOdd);
    }

    QVector<quint32> indices = node->uncookedIndexes();
    if (indices.size() > 0) {
        node->setColor(color);
        node->setFillGradient(pathData.gradient);

        node->cookGeometry();
        m_rootNode->appendChildNode(node);
        ret.append(node);
    }
    const bool wireFrame = debugVisualization() & DebugWireframe;
    if (wireFrame) {
        QQuickShapeWireFrameNode *wfNode = new QQuickShapeWireFrameNode;
        QSGGeometry *wfg = new QSGGeometry(QQuickShapeWireFrameNode::attributes(),
                                           wfVertices.size(),
                                           indices.size(),
                                           QSGGeometry::UnsignedIntType);
        wfNode->setGeometry(wfg);

        wfg->setDrawingMode(QSGGeometry::DrawTriangles);
        memcpy(wfg->indexData(),
               indices.data(),
               indices.size() * wfg->sizeOfIndex());
        memcpy(wfg->vertexData(),
               wfVertices.data(),
               wfg->vertexCount() * wfg->sizeOfVertex());

        m_rootNode->appendChildNode(wfNode);
        debugNodes->append(wfNode);
    }

    return ret;
}

void QQuickShapeCurveRenderer::setRootNode(QSGNode *node)
{
    m_rootNode = node;
}

int QQuickShapeCurveRenderer::debugVisualizationFlags = QQuickShapeCurveRenderer::NoDebug;

int QQuickShapeCurveRenderer::debugVisualization()
{
    static const int envFlags = qEnvironmentVariableIntValue("QT_QUICKSHAPES_DEBUG");
    return debugVisualizationFlags | envFlags;
}

void QQuickShapeCurveRenderer::setDebugVisualization(int options)
{
    if (debugVisualizationFlags == options)
        return;
    debugVisualizationFlags = options;
}

void QQuickShapeCurveRenderer::deleteAndClear(NodeList *nodeList)
{
    for (QSGNode *node : std::as_const(*nodeList))
        delete node;
    nodeList->clear();
}

QQuickShapeCurveRenderer::NodeList QQuickShapeCurveRenderer::addCurveStrokeNodes(const PathData &pathData, NodeList *debugNodes)
{
    NodeList ret;
    const QColor &color = pathData.pen.color();

    const bool debug = debugVisualization() & DebugCurves;
    auto *node = new QSGCurveStrokeNode;
    node->setDebug(0.2f * debug);
    QVector<QQuickShapeWireFrameNode::WireFrameVertex> wfVertices;

    const float miterLimit = pathData.pen.miterLimit();
    const float penWidth = pathData.pen.widthF();

    static const int subdivisions = qEnvironmentVariable("QT_QUICKSHAPES_STROKE_SUBDIVISIONS", QStringLiteral("3")).toInt();

    QSGCurveProcessor::processStroke(pathData.strokePath,
                                     miterLimit,
                                     penWidth,
                                     pathData.pen.joinStyle(),
                                     pathData.pen.capStyle(),
                                     [&wfVertices, &node](const std::array<QVector2D, 3> &s,
                                                          const std::array<QVector2D, 3> &p,
                                                          const std::array<QVector2D, 3> &n,
                                                          bool isLine)
                                    {
                                         const QVector2D &p0 = s.at(0);
                                         const QVector2D &p1 = s.at(1);
                                         const QVector2D &p2 = s.at(2);
                                         if (isLine)
                                             node->appendTriangle(s, std::array<QVector2D, 2>{p.at(0), p.at(2)}, n);
                                         else
                                             node->appendTriangle(s, p, n);

                                         wfVertices.append({p0.x(), p0.y(), 1.0f, 0.0f, 0.0f});
                                         wfVertices.append({p1.x(), p1.y(), 0.0f, 1.0f, 0.0f});
                                         wfVertices.append({p2.x(), p2.y(), 0.0f, 0.0f, 1.0f});
                                    },
                                    subdivisions);

    auto indexCopy = node->uncookedIndexes(); // uncookedIndexes get delete on cooking

    node->setColor(color);
    node->setStrokeWidth(pathData.pen.widthF());
    node->cookGeometry();
    m_rootNode->appendChildNode(node);
    ret.append(node);

    const bool wireFrame = debugVisualization() & DebugWireframe;
    if (wireFrame) {
        QQuickShapeWireFrameNode *wfNode = new QQuickShapeWireFrameNode;

        QSGGeometry *wfg = new QSGGeometry(QQuickShapeWireFrameNode::attributes(),
                                           wfVertices.size(),
                                           indexCopy.size(),
                                           QSGGeometry::UnsignedIntType);
        wfNode->setGeometry(wfg);

        wfg->setDrawingMode(QSGGeometry::DrawTriangles);
        memcpy(wfg->indexData(),
               indexCopy.data(),
               indexCopy.size() * wfg->sizeOfIndex());
        memcpy(wfg->vertexData(),
               wfVertices.data(),
               wfg->vertexCount() * wfg->sizeOfVertex());

        m_rootNode->appendChildNode(wfNode);
        debugNodes->append(wfNode);
    }

    return ret;
}

QT_END_NAMESPACE
