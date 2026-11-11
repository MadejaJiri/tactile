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
    // Narrower limits than the setting's own, where another setting bounds
    // this one. -1 leaves the setting's.
    property int lowest: -1
    property int highest: -1
    // A value worth pointing at, drawn as a tick on the slider. -1 for none.
    property int mark: -1
    // Something to do about the value, beside its number.
    property alias action: actionSlot.data

    readonly property int stored: backend.settings[key] ?? 0
    readonly property int shown: stored === 0 && zeroMeans > 0 ? zeroMeans : stored
    readonly property int from: lowest >= 0 ? lowest : zeroMeans > 0 ? 1 : backend.minimum(key)
    readonly property int to: Math.max(from + 1, highest >= 0 ? highest : backend.maximum(key))
    // Few enough values to show each one.
    readonly property bool stepped: to - from <= 8
    readonly property string valueText: {
        translator.language
        if (stored === 0 && zeroText !== "")
            return zeroText
        const number = Number(shown).toLocaleString(Qt.locale(), 'f', 0)
        const withUnit = unit === "" ? number : qsTr("%1 %2").arg(number).arg(unit)
        return stored === 0 && zeroMeans > 0 ? qsTr("%1 (default)").arg(withUnit) : withUnit
    }

    // Where a value sits along the slider, in its own coordinates.
    function xFor(value) {
        const travel = slider.availableWidth - slider.handle.width
        return slider.leftPadding + slider.handle.width / 2 + travel * (value - from) / (to - from)
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
        RowLayout {
            id: actionSlot
            spacing: 6
        }
        Label {
            text: row.valueText
            color: row.enabled ? "#b4b4bd" : "#6c6c75"
            font.pixelSize: 14
            font.features: { "tnum": 1 }
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        Layout.leftMargin: -6
        Layout.rightMargin: -6
        implicitHeight: 30
        from: row.from
        to: row.to
        stepSize: 1
        // The handle stands only where a value is, also while it is dragged.
        snapMode: Slider.SnapAlways
        value: row.shown
        onMoved: backend.set(row.key, Math.round(value))
        Accessible.name: row.label

        // One dot per value, where there are few.
        Repeater {
            model: row.stepped ? row.to - row.from + 1 : 0
            Rectangle {
                required property int index
                width: 4
                height: 4
                radius: 2
                x: row.xFor(row.from + index) - 2
                y: slider.height - 5
                color: row.enabled ? "#5c5c64" : "#3a3a42"
            }
        }

        Rectangle {
            visible: row.mark >= row.from && row.mark <= row.to
            width: 2
            height: 9
            radius: 1
            x: row.xFor(row.mark) - 1
            y: slider.height - 8
            color: row.enabled ? "#b4b4bd" : "#5c5c64"
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
