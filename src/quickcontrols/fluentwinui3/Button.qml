// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.Button {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    spacing: config.spacing || 0

    topPadding: config.topPadding || 0
    bottomPadding: config.bottomPadding || 0
    leftPadding: config.leftPadding || 0
    rightPadding: config.rightPadding || 0

    topInset: -config.topInset || 0
    bottomInset: -config.bottomInset || 0
    leftInset: -config.leftInset || 0
    rightInset: -config.rightInset || 0

    icon.width: 16
    icon.height: 16
    icon.color: __buttonText

    readonly property color __buttonText: {
        if (control.down) {
            return (control.checked || control.highlighted)
                ? Application.styleHints.colorScheme == Qt.Light
                    ? Qt.rgba(1, 1, 1, 0.7) : Qt.rgba(0, 0, 0, 0.5)
                : (Application.styleHints.colorScheme === Qt.Light
                    ? Qt.rgba(control.palette.buttonText.r, control.palette.buttonText.g, control.palette.buttonText.b, 0.62)
                    : Qt.rgba(control.palette.buttonText.r, control.palette.buttonText.g, control.palette.buttonText.b, 0.7725))
        } else if (control.checked || control.highlighted) {
            return (Application.styleHints.colorScheme === Qt.Dark && !control.enabled)
                ? Qt.rgba(1, 1, 1, 0.5302)
                : (Application.styleHints.colorScheme === Qt.Dark ? "black" : "white")
        } else {
            return control.palette.buttonText
        }
    }

    readonly property string __currentState: [
        (control.checked || control.highlighted) && "checked",
        !control.enabled && "disabled",
        control.enabled && !control.down && control.hovered && "hovered",
        control.down && "pressed"
    ].filter(Boolean).join("_") || "normal"
    readonly property var config: (control.flat && Config.controls.flatbutton
        ? Config.controls.flatbutton[__currentState]
        : Config.controls.button[__currentState]) || {}

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: control.config.label.textVAlignment | control.config.label.textHAlignment
        icon: control.icon
        text: control.text
        font: control.font
        color: control.icon.color
    }

    background: StyleImage {
        imageConfig: control.config.background
    }
}
