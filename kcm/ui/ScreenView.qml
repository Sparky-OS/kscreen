/*
    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.ScrollView {
    id: screenView

    required property var outputs
    required property bool interactive

    property size totalSize

    function resetTotalSize() {
        totalSize = kcm.normalizeScreen();
    }

    onWidthChanged: resetTotalSize()
    onHeightChanged: resetTotalSize()

    // The factor at which every screen fits into the view
    readonly property real fitFactor: {
        const relativeSize = Qt.size(
            totalSize.width / (0.6 * width),
            totalSize.height / (0.65 * height),
        );
        if (relativeSize.width > relativeSize.height) {
            // Available width smaller than height, optimize for width (we have
            // '>' because the available width, height is in the denominator).
            return relativeSize.width;
        } else {
            return relativeSize.height;
        }
    }

    // 1 shows every screen; the view pans by pan, in view pixels
    property real zoom: 1
    property point pan: Qt.point(0, 0)
    readonly property real minimumZoom: 0.25
    readonly property real maximumZoom: 16
    readonly property bool zoomed: zoom !== 1 || pan.x !== 0 || pan.y !== 0

    readonly property real relativeFactor: fitFactor / zoom

    readonly property int xOffset: (width - totalSize.width / relativeFactor) / 2 + pan.x;
    readonly property int yOffset: (height - totalSize.height / relativeFactor) / 2 + pan.y;

    // Zooms by factor, keeping the point of the arrangement under viewPoint where it is
    function zoomAt(viewPoint, factor) {
        const newZoom = Math.min(maximumZoom, Math.max(minimumZoom, zoom * factor));
        if (newZoom === zoom) {
            return;
        }
        const arrangementX = (viewPoint.x - xOffset) * relativeFactor;
        const arrangementY = (viewPoint.y - yOffset) * relativeFactor;
        const newFactor = fitFactor / newZoom;
        pan = Qt.point(viewPoint.x - arrangementX / newFactor - (width - totalSize.width / newFactor) / 2,
                       viewPoint.y - arrangementY / newFactor - (height - totalSize.height / newFactor) / 2);
        zoom = newZoom;
    }

    function zoomIn() {
        zoomAt(Qt.point(width / 2, height / 2), 1.25);
    }

    function zoomOut() {
        zoomAt(Qt.point(width / 2, height / 2), 0.8);
    }

    function fitAll() {
        zoom = 1;
        pan = Qt.point(0, 0);
        resetTotalSize();
    }

    clip: true

    WheelHandler {
        acceptedModifiers: Qt.ControlModifier
        onWheel: event => {
            const steps = event.angleDelta.y / 120;
            if (steps !== 0) {
                screenView.zoomAt(point.position, Math.pow(1.25, steps));
            }
        }
    }

    // Drag the empty arrangement, or anywhere with the middle button, to pan
    DragHandler {
        id: panHandler
        property point panStart
        target: null
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        grabPermissions: PointerHandler.CanTakeOverFromAnything | PointerHandler.TakeOverForbidden
        cursorShape: active ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onActiveChanged: {
            if (active) {
                panStart = screenView.pan;
            }
        }
        onTranslationChanged: {
            if (active) {
                screenView.pan = Qt.point(panStart.x + translation.x, panStart.y + translation.y);
            }
        }
    }

    Kirigami.Heading {
        id: dragHint
        z: 90
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            margins: Kirigami.Units.smallSpacing
        }
        level: 4
        horizontalAlignment: Text.AlignHCenter
        text: i18n("Drag screens to re-arrange them")

        opacity: interactive ? 0.6 : 0
        visible: opacity > 0
        Behavior on opacity {
            PropertyAnimation {
                easing.type: Easing.InOutQuad
                duration: Kirigami.Units.shortDuration
            }
        }
    }

    QQC2.Label {
        z: 90
        anchors {
            top: dragHint.bottom
            left: parent.left
            right: parent.right
            leftMargin: Kirigami.Units.smallSpacing
            rightMargin: Kirigami.Units.smallSpacing
        }
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font: Kirigami.Theme.smallFont
        text: i18nc("@info:usagetip", "Hold Shift while dragging to place a screen without snapping")

        opacity: dragHint.opacity
        visible: dragHint.visible
    }

    Repeater {
        model: kcm.outputModel
        delegate: Output {
            interactive: screenView.interactive
        }

        onCountChanged: resetTotalSize()
    }
}
