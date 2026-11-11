import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The label of a pressure line on the chart. Dragging it sets that pressure,
// so a threshold can be put right where the presses on screen peak.
Rectangle {
    id: handle

    property string key
    property string label
    property Item chart
    // Where the line it belongs to runs, in the chart's coordinates.
    property real lineY: 0

    width: text.implicitWidth + 18
    height: 20
    radius: Math.min(4, theme.radius)
    color: area.pressed ? "#3d3d45" : area.containsMouse ? "#303037" : "#232328"
    border.color: area.pressed ? theme.accent : "#3a3a42"

    Label {
        id: text
        anchors.centerIn: parent
        text: handle.label
        color: "#ececef"
        font.pixelSize: 11
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        // The page scrolls; without this a drag of a few pixels is taken away.
        preventStealing: true

        property real grab: 0
        onPressed: mouse => grab = handle.lineY - mapToItem(handle.chart, mouse.x, mouse.y).y
        onPositionChanged: mouse => {
            if (pressed)
                backend.set(handle.key, handle.chart.pressureAt(mapToItem(handle.chart, mouse.x, mouse.y).y + grab))
        }
    }
}
