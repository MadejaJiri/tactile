import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A plain button with the window's corners. "primary" fills it with the
// accent, to mark the choice in effect; "quiet" drops the outline for actions
// that sit inside a card; "subdued" keeps the outline and dims the rest, for
// an action that should not be the first thing reached for.
AbstractButton {
    id: control

    property bool primary: false
    property bool quiet: false
    property bool subdued: false

    implicitHeight: 34
    implicitWidth: label.implicitWidth + (quiet ? 20 : 30)
    hoverEnabled: true
    // Keys belong to the shortcuts; a focused button would take Space.
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: theme.radius
        color: control.primary ? theme.accent
             : control.down ? "#34343b"
             : control.hovered && control.enabled ? "#2a2a30"
             : control.quiet || control.subdued ? "transparent" : "#1e1e23"
        border.color: control.primary ? theme.accent : control.quiet ? "transparent" : "#33333a"
    }
    contentItem: Label {
        id: label
        text: control.text
        color: control.primary ? theme.accentForeground : !control.enabled ? "#5c5c64"
             : control.subdued ? "#9a9aa3" : "#ececef"
        font.pixelSize: 14
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
