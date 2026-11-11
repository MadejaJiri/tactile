import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One setting: its name, its value, and a slider between its limits.
ColumnLayout {
    id: row

    property string key
    property string label
    property string hint: ""
    property string unit: ""
    // Strength and softness store 0 for "the driver's default for this
    // trackpad"; the slider then stands at that default.
    property int zeroMeans: 0
    // What to call 0 where it switches something off.
    property string zeroText: ""

    readonly property int stored: backend.settings[key] ?? 0
    readonly property int shown: stored === 0 && zeroMeans > 0 ? zeroMeans : stored
    readonly property string valueText: {
        translator.language
        if (stored === 0 && zeroText !== "")
            return zeroText
        const number = Number(shown).toLocaleString(Qt.locale(), 'f', 0)
        const withUnit = unit === "" ? number : qsTr("%1 %2").arg(number).arg(unit)
        return stored === 0 && zeroMeans > 0 ? qsTr("%1 (default)").arg(withUnit) : withUnit
    }

    spacing: 0
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Label {
            text: row.label
            color: row.enabled ? "#ececef" : "#6c6c75"
            font.pixelSize: 14
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Label {
            text: row.valueText
            color: row.enabled ? "#b4b4bd" : "#6c6c75"
            font.pixelSize: 14
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        Layout.leftMargin: -6
        Layout.rightMargin: -6
        implicitHeight: 30
        from: row.zeroMeans > 0 ? 1 : backend.minimum(row.key)
        to: backend.maximum(row.key)
        stepSize: 1
        value: row.shown
        onMoved: backend.set(row.key, Math.round(value))
        Accessible.name: row.label
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
