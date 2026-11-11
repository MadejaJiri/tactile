import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A titled group of settings.
Rectangle {
    id: card

    property string title
    property string note: ""
    default property alias content: body.data
    property alias corner: cornerSlot.data

    color: "#17171a"
    border.color: "#26262b"
    radius: theme.radius
    implicitHeight: column.implicitHeight + 36
    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            // One height with or without buttons in the corner, so the
            // titles of neighbouring cards line up.
            Layout.preferredHeight: 34
            Layout.maximumHeight: 34
            spacing: 10

            Label {
                text: card.title
                color: "#ececef"
                font.pixelSize: 16
                font.bold: true
                Layout.fillWidth: true
            }
            RowLayout {
                id: cornerSlot
                spacing: 6
            }
        }

        Label {
            visible: card.note !== ""
            text: card.note
            color: "#9a9aa3"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ColumnLayout {
            id: body
            spacing: 10
            Layout.fillWidth: true
        }
    }
}
