/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtQml module of the Qt Toolkit.
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

#include "qv4compileddata_p.h"
#include <private/qv4staticvalue_p.h>
#include <private/qqmlirbuilder_p.h>
#include <QCoreApplication>
#include <QScopeGuard>
#include <QFileInfo>

#include <algorithm>

QT_BEGIN_NAMESPACE

namespace QV4 {

namespace CompiledData {

CompilationUnit::CompilationUnit(const Unit *unitData, const QString &fileName, const QString &finalUrlString)
{
    setUnitData(unitData, nullptr, fileName, finalUrlString);
}

CompilationUnit::~CompilationUnit()
{
    if (data) {
        if (data->qmlUnit() != qmlData)
            free(const_cast<QmlUnit *>(qmlData));
        qmlData = nullptr;

        if (!(data->flags & QV4::CompiledData::Unit::StaticData))
            free(const_cast<Unit *>(data));
    }
    data = nullptr;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    delete [] constants;
    constants = nullptr;
#endif

    delete [] imports;
    imports = nullptr;
}

void CompilationUnit::setUnitData(const Unit *unitData, const QmlUnit *qmlUnit,
                                  const QString &fileName, const QString &finalUrlString)
{
    data = unitData;
    qmlData = nullptr;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    delete [] constants;
#endif
    constants = nullptr;
    m_fileName.clear();
    m_finalUrlString.clear();
    if (!data)
        return;

    qmlData = qmlUnit ? qmlUnit : data->qmlUnit();

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    StaticValue *bigEndianConstants = new StaticValue[data->constantTableSize];
    const quint64_le *littleEndianConstants = data->constants();
    for (uint i = 0; i < data->constantTableSize; ++i)
        bigEndianConstants[i] = StaticValue::fromReturnedValue(littleEndianConstants[i]);
    constants = bigEndianConstants;
#else
    constants = reinterpret_cast<const StaticValue*>(data->constants());
#endif

    m_fileName = !fileName.isEmpty() ? fileName : stringAt(data->sourceFileIndex);
    m_finalUrlString = !finalUrlString.isEmpty() ? finalUrlString : stringAt(data->finalUrlIndex);
}

//reverse of Lexer::singleEscape()
QString Binding::escapedString(const QString &string)
{
    QString tmp = QLatin1String("\"");
    for (int i = 0; i < string.length(); ++i) {
        const QChar &c = string.at(i);
        switch (c.unicode()) {
        case 0x08:
            tmp += QLatin1String("\\b");
            break;
        case 0x09:
            tmp += QLatin1String("\\t");
            break;
        case 0x0A:
            tmp += QLatin1String("\\n");
            break;
        case 0x0B:
            tmp += QLatin1String("\\v");
            break;
        case 0x0C:
            tmp += QLatin1String("\\f");
            break;
        case 0x0D:
            tmp += QLatin1String("\\r");
            break;
        case 0x22:
            tmp += QLatin1String("\\\"");
            break;
        case 0x27:
            tmp += QLatin1String("\\\'");
            break;
        case 0x5C:
            tmp += QLatin1String("\\\\");
            break;
        default:
            tmp += c;
            break;
        }
    }
    tmp += QLatin1Char('\"');
    return tmp;
}

void CompilationUnit::unlink()
{
    free(runtimeStrings);
    runtimeStrings = nullptr;
    delete [] runtimeRegularExpressions;
    runtimeRegularExpressions = nullptr;
    free(runtimeClasses);
    runtimeClasses = nullptr;
}

}

}

QT_END_NAMESPACE
