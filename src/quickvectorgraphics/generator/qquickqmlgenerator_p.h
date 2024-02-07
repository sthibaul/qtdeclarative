// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#ifndef QQUICKQMLGENERATOR_P_H
#define QQUICKQMLGENERATOR_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "qquickgenerator_p.h"

#include <QtCore/qtextstream.h>

QT_BEGIN_NAMESPACE

class GeneratorStream;

class Q_QUICKVECTORGRAPHICSGENERATOR_EXPORT QQuickQmlGenerator : public QQuickGenerator
{
public:
    QQuickQmlGenerator(const QString fileName, QQuickVectorGraphics::GeneratorFlags flags, const QString &outFileName);
    ~QQuickQmlGenerator();

    void setShapeTypeName(const QString &name);
    QString shapeTypeName() const;

    void setCommentString(const QString commentString);
    QString commentString() const;

protected:
    void generateNodeBase(NodeInfo &info) override;
    bool generateDefsNode(NodeInfo &info) override;
    void generateImageNode(ImageNodeInfo &info) override;
    void generatePath(PathNodeInfo &info) override;
    void generateNode(NodeInfo &info) override;
    void generateTextNode(TextNodeInfo &info) override;
    void generateStructureNode(StructureNodeInfo &info) override;
    void generateRootNode(StructureNodeInfo &info) override;
    void generateGradient(const QGradient *grad, QQuickShapePath *shapePath, const QRectF &boundingRect) override;
    void outputShapePath(const PathNodeInfo &info, const QPainterPath *path, const QQuadPath *quadPath, QQuickVectorGraphics::PathSelector pathSelector, const QRectF &boundingRect) override;

private:
    QString indent();
    GeneratorStream stream();
    const char *shapeName() const;

private:
    int m_indentLevel = 0;
    QTextStream *m_stream;
    QByteArray result;
    QString outputFileName;
    bool m_inShapeItem = false;
    QByteArray m_shapeTypeName;
    QString m_commentString;
};

QT_END_NAMESPACE

#endif // QQUICKQMLGENERATOR_P_H