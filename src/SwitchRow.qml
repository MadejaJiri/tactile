import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One setting that is on or off.
ColumnLayout {
    id: row

    property string key
    property string label
    property string hint: ""

    spacing: 0
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Label {
            text: row.label
            color: row.enabled ? "#ececef" : "#6c6c75"
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Switch {
            checked: backend.settings[row.key] === 1
            focusPolicy: Qt.NoFocus
            onToggled: backend.set(row.key, checked ? 1 : 0)
            Accessible.name: row.label
        }
    }

    Label {
        visible: row.hint !== ""
        text: row.hint
        color: row.enabled ? "#8b8b94" : "#5c5c64"
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.bottomMargin: 2
    }
}
