/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Copyright (C) 2016 BasysKom GmbH.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtQuick module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtQuick/private/qquickvaluetypes_p.h>
#include <QtQuick/private/qquickapplication_p.h>
#include <QtQuick/private/qquickstate_p.h>
#include <QtQuick/private/qquickpropertychanges_p.h>
#include <QtQuick/private/qquickitemsmodule_p.h>
#include <QtQuick/private/qquickaccessiblefactory_p.h>

#include <QtGui/QGuiApplication>
#include <QtGui/qdesktopservices.h>
#include <QtGui/qfontdatabase.h>
#include <QtGui/qstylehints.h>

#include <QtQml/private/qqmlbinding_p.h>
#include <QtQml/private/qqmldebugserviceinterfaces_p.h>
#include <QtQml/private/qqmldebugstatesdelegate_p.h>
#include <QtQml/private/qqmlglobal_p.h>
#include <QtQml/private/qv4engine_p.h>
#include <QtQml/private/qv4object_p.h>

#include <QtCore/qiterable.h>

#ifdef Q_CC_MSVC
// MSVC2010 warns about 'unused variable t', even if it's used in t->~T()
#  pragma warning( disable : 4189 )
#endif

QT_BEGIN_NAMESPACE

#if QT_CONFIG(qml_debug)

class QQmlQtQuick2DebugStatesDelegate : public QQmlDebugStatesDelegate
{
public:
    QQmlQtQuick2DebugStatesDelegate();
    ~QQmlQtQuick2DebugStatesDelegate();
    void buildStatesList(bool cleanList, const QList<QPointer<QObject> > &instances) override;
    void updateBinding(QQmlContext *context,
                       const QQmlProperty &property,
                       const QVariant &expression, bool isLiteralValue,
                       const QString &fileName, int line, int column,
                       bool *isBaseState) override;
    bool setBindingForInvalidProperty(QObject *object,
                                      const QString &propertyName,
                                      const QVariant &expression,
                                      bool isLiteralValue) override;
    void resetBindingForInvalidProperty(QObject *object,
                                        const QString &propertyName) override;

private:
    void buildStatesList(QObject *obj);

    QList<QPointer<QQuickState> > m_allStates;
};

QQmlQtQuick2DebugStatesDelegate::QQmlQtQuick2DebugStatesDelegate()
{
}

QQmlQtQuick2DebugStatesDelegate::~QQmlQtQuick2DebugStatesDelegate()
{
}

void QQmlQtQuick2DebugStatesDelegate::buildStatesList(bool cleanList,
                                                      const QList<QPointer<QObject> > &instances)
{
    if (cleanList)
        m_allStates.clear();

    //only root context has all instances
    for (int ii = 0; ii < instances.count(); ++ii) {
        buildStatesList(instances.at(ii));
    }
}

void QQmlQtQuick2DebugStatesDelegate::buildStatesList(QObject *obj)
{
    if (QQuickState *state = qobject_cast<QQuickState *>(obj)) {
        m_allStates.append(state);
    }

    QObjectList children = obj->children();
    for (int ii = 0; ii < children.count(); ++ii) {
        buildStatesList(children.at(ii));
    }
}

void QQmlQtQuick2DebugStatesDelegate::updateBinding(QQmlContext *context,
                                                            const QQmlProperty &property,
                                                            const QVariant &expression, bool isLiteralValue,
                                                            const QString &fileName, int line, int column,
                                                            bool *inBaseState)
{
    Q_UNUSED(column);
    typedef QPointer<QQuickState> QuickStatePointer;
    QObject *object = property.object();
    QString propertyName = property.name();
    for (const QuickStatePointer& statePointer : qAsConst(m_allStates)) {
        if (QQuickState *state = statePointer.data()) {
            // here we assume that the revert list on itself defines the base state
            if (state->isStateActive() && state->containsPropertyInRevertList(object, propertyName)) {
                *inBaseState = false;

                QQmlBinding *newBinding = nullptr;
                if (!isLiteralValue) {
                    newBinding = QQmlBinding::create(&QQmlPropertyPrivate::get(property)->core,
                                                     expression.toString(), object,
                                                     QQmlContextData::get(context), fileName,
                                                     line);
                    newBinding->setTarget(property);
                }

                state->changeBindingInRevertList(object, propertyName, newBinding);

                if (isLiteralValue)
                    state->changeValueInRevertList(object, propertyName, expression);
            }
        }
    }
}

bool QQmlQtQuick2DebugStatesDelegate::setBindingForInvalidProperty(QObject *object,
                                                                           const QString &propertyName,
                                                                           const QVariant &expression,
                                                                           bool isLiteralValue)
{
    if (QQuickPropertyChanges *propertyChanges = qobject_cast<QQuickPropertyChanges *>(object)) {
        if (isLiteralValue)
            propertyChanges->changeValue(propertyName, expression);
        else
            propertyChanges->changeExpression(propertyName, expression.toString());
        return true;
    } else {
        return false;
    }
}

void QQmlQtQuick2DebugStatesDelegate::resetBindingForInvalidProperty(QObject *object, const QString &propertyName)
{
    if (QQuickPropertyChanges *propertyChanges = qobject_cast<QQuickPropertyChanges *>(object)) {
        propertyChanges->removeProperty(propertyName);
    }
}

static QQmlDebugStatesDelegate *statesDelegateFactory()
{
    return new QQmlQtQuick2DebugStatesDelegate;
}

#endif // QT_CONFIG(qml_debug)

class QQuickColorProvider : public QQmlColorProvider
{
public:
    QVariant colorFromString(const QString &s, bool *ok) override
    {
        QColor c(s);
        if (c.isValid()) {
            if (ok) *ok = true;
            return QVariant(c);
        }

        if (ok) *ok = false;
        return QVariant();
    }

    unsigned rgbaFromString(const QString &s, bool *ok) override
    {
        QColor c(s);
        if (c.isValid()) {
            if (ok) *ok = true;
            return c.rgba();
        }

        if (ok) *ok = false;
        return 0;
    }

    QString stringFromRgba(unsigned rgba)
    {
        QColor c(QColor::fromRgba(rgba));
        if (c.isValid()) {
            return QVariant(c).toString();
        }

        return QString();
    }

    QVariant fromRgbF(double r, double g, double b, double a) override
    {
        return QVariant(QColor::fromRgbF(r, g, b, a));
    }

    QVariant fromHslF(double h, double s, double l, double a) override
    {
        return QVariant(QColor::fromHslF(h, s, l, a));
    }

    QVariant fromHsvF(double h, double s, double v, double a) override
    {
        return QVariant(QColor::fromHsvF(h, s, v, a));
    }

    QVariant lighter(const QVariant &var, qreal factor) override
    {
        QColor color = var.value<QColor>();
        color = color.lighter(int(qRound(factor*100.)));
        return QVariant::fromValue(color);
    }

    QVariant darker(const QVariant &var, qreal factor) override
    {
        QColor color = var.value<QColor>();
        color = color.darker(int(qRound(factor*100.)));
        return QVariant::fromValue(color);
    }

    QVariant alpha(const QVariant &var, qreal value) override
    {
        QColor color = var.value<QColor>();
        color.setAlphaF(value);
        return QVariant::fromValue(color);
    }

    QVariant tint(const QVariant &baseVar, const QVariant &tintVar) override
    {
        QColor tintColor = tintVar.value<QColor>();

        int tintAlpha = tintColor.alpha();
        if (tintAlpha == 0xFF) {
            return tintVar;
        } else if (tintAlpha == 0x00) {
            return baseVar;
        }

        // tint the base color and return the final color
        QColor baseColor = baseVar.value<QColor>();
        qreal a = tintColor.alphaF();
        qreal inv_a = 1.0 - a;

        qreal r = tintColor.redF() * a + baseColor.redF() * inv_a;
        qreal g = tintColor.greenF() * a + baseColor.greenF() * inv_a;
        qreal b = tintColor.blueF() * a + baseColor.blueF() * inv_a;

        return QVariant::fromValue(QColor::fromRgbF(r, g, b, a + inv_a * baseColor.alphaF()));
    }
};


// Note: The functions in this class provide handling only for the types
// that the QML engine will currently actually call them for, so many
// appear incompletely implemented.  For some functions, the implementation
// would be obvious, but for others (particularly create and createFromString)
// the exact semantics are unknown.  For this reason unused functionality
// has been omitted.

class QQuickValueTypeProvider : public QQmlValueTypeProvider
{
public:

#if defined(QT_NO_DEBUG) && !defined(QT_FORCE_ASSERTS)
    #define ASSERT_VALID_SIZE(size, min) Q_UNUSED(size);
#else
    #define ASSERT_VALID_SIZE(size, min) Q_ASSERT(size >= min)
#endif

    static QVector2D vector2DFromString(const QString &s, bool *ok)
    {
        if (s.count(QLatin1Char(',')) == 1) {
            int index = s.indexOf(QLatin1Char(','));

            bool xGood, yGood;
            float xCoord = QStringView{s}.left(index).toFloat(&xGood);
            float yCoord = QStringView{s}.mid(index + 1).toFloat(&yGood);

            if (xGood && yGood) {
                if (ok) *ok = true;
                return QVector2D(xCoord, yCoord);
            }
        }

        if (ok) *ok = false;
        return QVector2D();
    }

    static QVector3D vector3DFromString(const QString &s, bool *ok)
    {
        if (s.count(QLatin1Char(',')) == 2) {
            int index = s.indexOf(QLatin1Char(','));
            int index2 = s.indexOf(QLatin1Char(','), index+1);

            bool xGood, yGood, zGood;
            float xCoord = QStringView{s}.left(index).toFloat(&xGood);
            float yCoord = QStringView{s}.mid(index + 1, index2 - index - 1).toFloat(&yGood);
            float zCoord = QStringView{s}.mid(index2 + 1).toFloat(&zGood);

            if (xGood && yGood && zGood) {
                if (ok) *ok = true;
                return QVector3D(xCoord, yCoord, zCoord);
            }
        }

        if (ok) *ok = false;
        return QVector3D();
    }

    static QVector4D vector4DFromString(const QString &s, bool *ok)
    {
        if (s.count(QLatin1Char(',')) == 3) {
            int index = s.indexOf(QLatin1Char(','));
            int index2 = s.indexOf(QLatin1Char(','), index+1);
            int index3 = s.indexOf(QLatin1Char(','), index2+1);

            bool xGood, yGood, zGood, wGood;
            float xCoord = QStringView{s}.left(index).toFloat(&xGood);
            float yCoord = QStringView{s}.mid(index + 1, index2 - index - 1).toFloat(&yGood);
            float zCoord = QStringView{s}.mid(index2 + 1, index3 - index2 - 1).toFloat(&zGood);
            float wCoord = QStringView{s}.mid(index3 + 1).toFloat(&wGood);

            if (xGood && yGood && zGood && wGood) {
                if (ok) *ok = true;
                return QVector4D(xCoord, yCoord, zCoord, wCoord);
            }
        }

        if (ok) *ok = false;
        return QVector4D();
    }

    static QQuaternion quaternionFromString(const QString &s, bool *ok)
    {
        if (s.count(QLatin1Char(',')) == 3) {
            int index = s.indexOf(QLatin1Char(','));
            int index2 = s.indexOf(QLatin1Char(','), index+1);
            int index3 = s.indexOf(QLatin1Char(','), index2+1);

            bool sGood, xGood, yGood, zGood;
            qreal sCoord = QStringView{s}.left(index).toDouble(&sGood);
            qreal xCoord = QStringView{s}.mid(index+1, index2-index-1).toDouble(&xGood);
            qreal yCoord = QStringView{s}.mid(index2+1, index3-index2-1).toDouble(&yGood);
            qreal zCoord = QStringView{s}.mid(index3+1).toDouble(&zGood);

            if (sGood && xGood && yGood && zGood) {
                if (ok) *ok = true;
                return QQuaternion(sCoord, xCoord, yCoord, zCoord);
            }
        }

        if (ok) *ok = false;
        return QQuaternion();
    }

    static QMatrix4x4 matrix4x4FromString(const QString &s, bool *ok)
    {
        if (s.count(QLatin1Char(',')) == 15) {
            float matValues[16];
            bool vOK = true;
            QStringView mutableStr(s);
            for (int i = 0; vOK && i < 16; ++i) {
                int cidx = mutableStr.indexOf(QLatin1Char(','));
                matValues[i] = mutableStr.left(cidx).toDouble(&vOK);
                mutableStr = mutableStr.mid(cidx + 1);
            }

            if (vOK) {
                if (ok) *ok = true;
                return QMatrix4x4(matValues);
            }
        }

        if (ok) *ok = false;
        return QMatrix4x4();
    }

    static QColorSpace colorSpaceFromObject(const QV4::Value &object, QV4::ExecutionEngine *v4, bool *ok)
    {
        if (ok)
            *ok = false;
        QColorSpace retn;
        QV4::Scope scope(v4);
        QV4::ScopedObject obj(scope, object);
        if (!obj) {
            if (ok)
                *ok = false;
            return retn;
        }

        QV4::ScopedString s(scope);

        QV4::ScopedValue vName(scope, obj->get((s = v4->newString(QStringLiteral("namedColorSpace")))));
        if (vName->isInt32()) {
            if (ok)
                *ok = true;
            return QColorSpace((QColorSpace::NamedColorSpace)vName->toInt32());
        }

        QV4::ScopedValue vPri(scope, obj->get((s = v4->newString(QStringLiteral("primaries")))));
        QV4::ScopedValue vTra(scope, obj->get((s = v4->newString(QStringLiteral("transferFunction")))));
        if (!vPri->isInt32() || !vTra->isInt32()) {
            if (ok)
                *ok = false;
            return retn;
        }

        QColorSpace::Primaries pri = static_cast<QColorSpace::Primaries>(vPri->integerValue());
        QColorSpace::TransferFunction tra = static_cast<QColorSpace::TransferFunction>(vTra->integerValue());
        float gamma = 0.0f;
        if (tra == QColorSpace::TransferFunction::Gamma) {
            QV4::ScopedValue vGam(scope, obj->get((s = v4->newString(QStringLiteral("gamma")))));
            if (!vGam->isNumber()) {
                if (ok)
                    *ok = false;
                return retn;
            }
            gamma = vGam->toNumber();
        }
        if (ok) *ok = true;
        return QColorSpace(pri, tra, gamma);
    }

    static QFont fontFromObject(const QV4::Value &object, QV4::ExecutionEngine *v4, bool *ok)
    {
        if (ok)
            *ok = false;
        QFont retn;
        QV4::Scope scope(v4);
        QV4::ScopedObject obj(scope, object);
        if (!obj) {
            if (ok)
                *ok = false;
            return retn;
        }

        QV4::ScopedString s(scope);

        QV4::ScopedValue vbold(scope, obj->get((s = v4->newString(QStringLiteral("bold")))));
        QV4::ScopedValue vcap(scope, obj->get((s = v4->newString(QStringLiteral("capitalization")))));
        QV4::ScopedValue vfam(scope, obj->get((s = v4->newString(QStringLiteral("family")))));
        QV4::ScopedValue vstyle(scope, obj->get((s = v4->newString(QStringLiteral("styleName")))));
        QV4::ScopedValue vital(scope, obj->get((s = v4->newString(QStringLiteral("italic")))));
        QV4::ScopedValue vlspac(scope, obj->get((s = v4->newString(QStringLiteral("letterSpacing")))));
        QV4::ScopedValue vpixsz(scope, obj->get((s = v4->newString(QStringLiteral("pixelSize")))));
        QV4::ScopedValue vpntsz(scope, obj->get((s = v4->newString(QStringLiteral("pointSize")))));
        QV4::ScopedValue vstrk(scope, obj->get((s = v4->newString(QStringLiteral("strikeout")))));
        QV4::ScopedValue vundl(scope, obj->get((s = v4->newString(QStringLiteral("underline")))));
        QV4::ScopedValue vweight(scope, obj->get((s = v4->newString(QStringLiteral("weight")))));
        QV4::ScopedValue vwspac(scope, obj->get((s = v4->newString(QStringLiteral("wordSpacing")))));
        QV4::ScopedValue vhint(scope, obj->get((s = v4->newString(QStringLiteral("hintingPreference")))));
        QV4::ScopedValue vkerning(scope, obj->get((s = v4->newString(QStringLiteral("kerning")))));
        QV4::ScopedValue vshaping(scope, obj->get((s = v4->newString(QStringLiteral("preferShaping")))));

        // pull out the values, set ok to true if at least one valid field is given.
        if (vbold->isBoolean()) {
            retn.setBold(vbold->booleanValue());
            if (ok) *ok = true;
        }
        if (vcap->isInt32()) {
            retn.setCapitalization(static_cast<QFont::Capitalization>(vcap->integerValue()));
            if (ok) *ok = true;
        }
        if (vfam->isString()) {
            retn.setFamily(vfam->toQString());
            if (ok) *ok = true;
        }
        if (vstyle->isString()) {
            retn.setStyleName(vstyle->toQString());
            if (ok) *ok = true;
        }
        if (vital->isBoolean()) {
            retn.setItalic(vital->booleanValue());
            if (ok) *ok = true;
        }
        if (vlspac->isNumber()) {
            retn.setLetterSpacing(QFont::AbsoluteSpacing, vlspac->asDouble());
            if (ok) *ok = true;
        }
        if (vpixsz->isInt32()) {
            retn.setPixelSize(vpixsz->integerValue());
            if (ok) *ok = true;
        }
        if (vpntsz->isNumber()) {
            retn.setPointSize(vpntsz->asDouble());
            if (ok) *ok = true;
        }
        if (vstrk->isBoolean()) {
            retn.setStrikeOut(vstrk->booleanValue());
            if (ok) *ok = true;
        }
        if (vundl->isBoolean()) {
            retn.setUnderline(vundl->booleanValue());
            if (ok) *ok = true;
        }
        if (vweight->isInt32()) {
            retn.setWeight(QFont::Weight(vweight->integerValue()));
            if (ok) *ok = true;
        }
        if (vwspac->isNumber()) {
            retn.setWordSpacing(vwspac->asDouble());
            if (ok) *ok = true;
        }
        if (vhint->isInt32()) {
            retn.setHintingPreference(static_cast<QFont::HintingPreference>(vhint->integerValue()));
            if (ok) *ok = true;
        }
        if (vkerning->isBoolean()) {
            retn.setKerning(vkerning->booleanValue());
            if (ok) *ok = true;
        }
        if (vshaping->isBoolean()) {
            bool enable = vshaping->booleanValue();
            if (enable)
                retn.setStyleStrategy(static_cast<QFont::StyleStrategy>(retn.styleStrategy() & ~QFont::PreferNoShaping));
            else
                retn.setStyleStrategy(static_cast<QFont::StyleStrategy>(retn.styleStrategy() | QFont::PreferNoShaping));
        }

        return retn;
    }

    static QMatrix4x4 matrix4x4FromObject(const QV4::Value &object, QV4::ExecutionEngine *v4, bool *ok)
    {
        if (ok)
            *ok = false;
        QV4::Scope scope(v4);
        QV4::ScopedArrayObject array(scope, object);
        if (!array)
            return QMatrix4x4();

        if (array->getLength() != 16)
            return QMatrix4x4();

        float matVals[16];
        QV4::ScopedValue v(scope);
        for (quint32 i = 0; i < 16; ++i) {
            v = array->get(i);
            if (!v->isNumber())
                return QMatrix4x4();
            matVals[i] = v->asDouble();
        }

        if (ok) *ok = true;
        return QMatrix4x4(matVals);
    }

    bool create(int type, const QJSValue &params, QVariant *v) override
    {
        switch (type) {
        case QMetaType::QFont: // must specify via js-object.
            break;
        case QMetaType::QVector2D:
            if (params.isArray()) {
                *v = QVariant(QVector2D(params.property(0).toNumber(),
                                        params.property(1).toNumber()));
                return true;
            }
            break;
        case QMetaType::QVector3D:
            if (params.isArray()) {
                *v = QVariant(QVector3D(params.property(0).toNumber(),
                                        params.property(1).toNumber(),
                                        params.property(2).toNumber()));
                return true;
            }
            break;
        case QMetaType::QVector4D:
            if (params.isArray()) {
                *v = QVariant(QVector4D(params.property(0).toNumber(),
                                        params.property(1).toNumber(),
                                        params.property(2).toNumber(),
                                        params.property(3).toNumber()));
                return true;
            }
            break;
        case QMetaType::QQuaternion:
            if (params.isArray()) {
                *v = QVariant(QQuaternion(params.property(0).toNumber(),
                                          params.property(1).toNumber(),
                                          params.property(2).toNumber(),
                                          params.property(3).toNumber()));
                return true;
            }
            break;
        case QMetaType::QMatrix4x4:
            if (params.isNull() || params.isUndefined()) {
                QMatrix4x4 m;
                *v = QVariant(m);
                return true;
            } else if (params.isArray()) {
                *v = QVariant(QMatrix4x4(params.property(0).toNumber(),
                                         params.property(1).toNumber(),
                                         params.property(2).toNumber(),
                                         params.property(3).toNumber(),
                                         params.property(4).toNumber(),
                                         params.property(5).toNumber(),
                                         params.property(6).toNumber(),
                                         params.property(7).toNumber(),
                                         params.property(8).toNumber(),
                                         params.property(9).toNumber(),
                                         params.property(10).toNumber(),
                                         params.property(11).toNumber(),
                                         params.property(12).toNumber(),
                                         params.property(13).toNumber(),
                                         params.property(14).toNumber(),
                                         params.property(15).toNumber()));
                return true;
            }
            break;
        default: break;
        }

        return false;
    }

    template<typename T>
    bool createFromStringTyped(void *data, size_t dataSize, T initValue)
    {
        ASSERT_VALID_SIZE(dataSize, sizeof(T));
        T *t = reinterpret_cast<T *>(data);
        new (t) T(initValue);
        return true;
    }

    bool createFromString(int type, const QString &s, void *data, size_t dataSize) override
    {
        bool ok = false;

        switch (type) {
        case QMetaType::QColor:
            return createFromStringTyped<QColor>(data, dataSize, QColor(s));
        case QMetaType::QVector2D:
            return createFromStringTyped<QVector2D>(data, dataSize, vector2DFromString(s, &ok));
        case QMetaType::QVector3D:
            return createFromStringTyped<QVector3D>(data, dataSize, vector3DFromString(s, &ok));
        case QMetaType::QVector4D:
            return createFromStringTyped<QVector4D>(data, dataSize, vector4DFromString(s, &ok));
        case QMetaType::QQuaternion:
            return createFromStringTyped<QQuaternion>(data, dataSize, quaternionFromString(s, &ok));
        case QMetaType::QMatrix4x4:
            return createFromStringTyped<QMatrix4x4>(data, dataSize, matrix4x4FromString(s, &ok));
        default: break;
        }

        return false;
    }

    bool variantFromString(int type, const QString &s, QVariant *v) override
    {
        bool ok = false;

        switch (type) {
        case QMetaType::QColor:
            {
            QColor c(s);
            *v = QVariant::fromValue(c);
            return true;
            }
        case QMetaType::QVector2D:
            {
            *v = QVariant::fromValue(vector2DFromString(s, &ok));
            return true;
            }
        case QMetaType::QVector3D:
            {
            *v = QVariant::fromValue(vector3DFromString(s, &ok));
            return true;
            }
        case QMetaType::QVector4D:
            {
            *v = QVariant::fromValue(vector4DFromString(s, &ok));
            return true;
            }
        case QMetaType::QQuaternion:
            {
            *v = QVariant::fromValue(quaternionFromString(s, &ok));
            return true;
            }
        case QMetaType::QMatrix4x4:
            {
            *v = QVariant::fromValue(matrix4x4FromString(s, &ok));
            return true;
            }
        default:
            break;
        }

        return false;
    }

    bool variantFromJsObject(int type, const QV4::Value &object, QV4::ExecutionEngine *v4, QVariant *v) override
    {
        QV4::Scope scope(v4);
#ifndef QT_NO_DEBUG
        QV4::ScopedObject obj(scope, object);
        Q_ASSERT(obj);
#endif
        bool ok = false;
        switch (type) {
        case QMetaType::QColorSpace:
            *v = QVariant::fromValue(colorSpaceFromObject(object, v4, &ok));
            break;
        case QMetaType::QFont:
            *v = QVariant::fromValue(fontFromObject(object, v4, &ok));
            break;
        case QMetaType::QMatrix4x4:
            *v = QVariant::fromValue(matrix4x4FromObject(object, v4, &ok));
        default: break;
        }

        return ok;
    }

    template<typename T>
    bool typedStore(const void *src, void *dst, size_t dstSize)
    {
        ASSERT_VALID_SIZE(dstSize, sizeof(T));
        const T *srcT = reinterpret_cast<const T *>(src);
        T *dstT = reinterpret_cast<T *>(dst);
        new (dstT) T(*srcT);
        return true;
    }

    bool store(int type, const void *src, void *dst, size_t dstSize) override
    {
        Q_UNUSED(dstSize);
        switch (type) {
        case QMetaType::QColor:
            {
            Q_ASSERT(dstSize >= sizeof(QColor));
            const QRgb *rgb = reinterpret_cast<const QRgb *>(src);
            QColor *color = reinterpret_cast<QColor *>(dst);
            new (color) QColor(QColor::fromRgba(*rgb));
            return true;
            }
            default: break;
        }

        return false;
    }

#undef ASSERT_VALID_SIZE
};


class QQuickGuiProvider : public QQmlGuiProvider
{
public:
    QQuickApplication *application(QObject *parent) override
    {
        return new QQuickApplication(parent);
    }

#if QT_CONFIG(im)
    QInputMethod *inputMethod() override
    {
        QInputMethod *im = qGuiApp->inputMethod();
        QQmlEngine::setObjectOwnership(im, QQmlEngine::CppOwnership);
        return im;
    }
#endif

    QStyleHints *styleHints() override
    {
        QStyleHints *sh = qGuiApp->styleHints();
        QQmlEngine::setObjectOwnership(sh, QQmlEngine::CppOwnership);
        return sh;
    }

    QStringList fontFamilies() override
    {
        QFontDatabase database;
        return database.families();
    }

    bool openUrlExternally(QUrl &url) override
    {
#ifndef QT_NO_DESKTOPSERVICES
        return QDesktopServices::openUrl(url);
#else
        Q_UNUSED(url);
        return false;
#endif
    }

    QString pluginName() const override
    {
        return QGuiApplication::platformName();
    }
};


static QQuickValueTypeProvider *getValueTypeProvider()
{
    static QQuickValueTypeProvider valueTypeProvider;
    return &valueTypeProvider;
}

static QQuickColorProvider *getColorProvider()
{
    static QQuickColorProvider colorProvider;
    return &colorProvider;
}

static QQuickGuiProvider *getGuiProvider()
{
    static QQuickGuiProvider guiProvider;
    return &guiProvider;
}

void QQuick_initializeModule()
{
    // This is used by QQuickPath, and on macOS it fails to automatically register.
    qRegisterMetaType<QVector<QVector<QPointF>>>();

    QQml_addValueTypeProvider(getValueTypeProvider());
    QQml_setColorProvider(getColorProvider());
    QQml_setGuiProvider(getGuiProvider());

    QQuickItemsModule::defineModule();

#if QT_CONFIG(accessibility)
    QAccessible::installFactory(&qQuickAccessibleFactory);
#endif

#if QT_CONFIG(qml_debug)
    QQmlEngineDebugService::setStatesDelegateFactory(statesDelegateFactory);
#endif
}

Q_CONSTRUCTOR_FUNCTION(QQuick_initializeModule)

QT_END_NAMESPACE
