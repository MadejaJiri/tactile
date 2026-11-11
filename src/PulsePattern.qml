import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What a click plays, drawn against time: one bar per pulse, as tall as the
// pulse is strong, as far apart as the gap between them. It shows when the
// actuator fires, not how the pad moves during one pulse; that shape is the
// trackpad's own.
Item {
    id: pattern

    // Each group: { label, pulses, gap (ms), strength (1 to 255) }.
    property var groups: []
    property color accent: "#FFD60A"

    implicitHeight: 86
    Layout.fillWidth: true

    // Milliseconds drawn per pixel, so that the widest group still fits.
    readonly property real groupWidth: (width - (groups.length - 1) * 16) / Math.max(1, groups.length)
    readonly property real scale: {
        let widest = 1
        for (const group of groups)
            widest = Math.max(widest, (group.pulses - 1) * group.gap)
        return Math.min(1.6, (groupWidth - 24) / widest)
    }

    Row {
        anchors.fill: parent
        spacing: 16

        Repeater {
            model: pattern.groups

            Item {
                id: group
                required property var modelData
                width: pattern.groupWidth
                height: pattern.height

                Rectangle {
                    // The line the pulses stand on.
                    x: 0
                    y: parent.height - 24
                    width: parent.width
                    height: 1
                    color: "#33333a"
                }
                Repeater {
                    model: group.modelData.pulses
                    Rectangle {
                        required property int index
                        readonly property real tall: Math.max(3, (group.height - 30) * group.modelData.strength / 255)
                        x: 8 + index * group.modelData.gap * pattern.scale
                        y: group.height - 24 - tall
                        width: 4
                        height: tall
                        radius: 2
                        color: pattern.enabled ? pattern.accent : "#4a4a52"
                    }
                }
                Label {
                    visible: group.modelData.pulses === 0
                    x: 8
                    y: group.height - 46
                    text: "–"
                    color: "#6c6c75"
                    font.pixelSize: 14
                }
                Label {
                    anchors { left: parent.left; bottom: parent.bottom }
                    width: parent.width
                    text: group.modelData.label
                    color: pattern.enabled ? "#9a9aa3" : "#5c5c64"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }
}
