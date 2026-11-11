import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Tactile

ApplicationWindow {
    id: win
    width: 1180
    height: 940
    minimumWidth: 760
    minimumHeight: 540
    visible: true
    title: "Tactile"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    readonly property bool calibrating: backend.calibrationStep !== Backend.Idle
    // Whether the driver plays the click, which is what every setting below
    // the presets is about.
    readonly property bool hostClick: backend.settings.hostClick === 1
    readonly property bool forceClick: hostClick && backend.settings.forceClick === 1
    readonly property bool idle: !calibrating && !helpVisible

    readonly property string serviceCommand: "sudo systemctl enable --now tactiled"
    readonly property string driverCommand: "sudo tactile-driver reload"
    readonly property string repairCommand: "sudo tactile-driver repair"
    readonly property string problemCommand: backend.problem === "daemon" ? serviceCommand
                                           : backend.problem === "stock" || backend.problem === "outdated" ? driverCommand
                                           : backend.problem === "noactuator" ? repairCommand : ""
    readonly property string problemText: {
        switch (backend.problem) {
        case "daemon": return qsTr("Tactile's service is not running. Start it with: %1").arg(serviceCommand)
        case "refused": return qsTr("The service only listens to the user sitting at this computer.")
        case "missing": return qsTr("The trackpad driver (hid-magicmouse) is not loaded.")
        case "stock": return qsTr("The kernel's own trackpad driver is loaded, so the settings below are saved but not in effect. Load Tactile's driver with: %1").arg(driverCommand)
        case "outdated": return qsTr("The loaded driver is older than this version of Tactile, so some of the settings below do nothing yet. The new one takes over at the next boot, or with: %1").arg(driverCommand)
        case "notrackpad": return qsTr("No Apple trackpad found.")
        case "noactuator": return qsTr("The trackpad's actuator, the part that makes the click, was not found at this boot, so the firmware still plays the click. Find it again with: %1").arg(repairCommand)
        case "failed": return qsTr("The trackpad refused to hand over the click, so its firmware still plays it. Lift your fingers and try again.")
        case "errors": return qsTr("The driver refused: %1").arg(backend.errors.join("; "))
        }
        return ""
    }

    function num(value) {
        // Read so the bindings that call this re-render on a language switch.
        translator.language
        return Number(value).toLocaleString(Qt.locale(), 'f', 0)
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.calibrating; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.testClick("press") }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo]; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.redo() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.applyPreset("light") }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.applyPreset("medium") }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.applyPreset("firm") }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.toggle("hostClick") }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: win.idle && win.hostClick; onActivated: backend.matchFeedback() }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.nudge(-1) }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.nudge(1) }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.startCalibration() }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: win.idle && win.hostClick; onActivated: backend.toggle("forceClick") }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.reset() }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: win.idle; onActivated: backend.paused = !backend.paused }
    Shortcut {
        sequences: ["Return", "Enter"]
        context: Qt.ApplicationShortcut
        enabled: backend.calibrationStep === Backend.Done
        onActivated: backend.closeCalibration()
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: win.calibrating || win.helpVisible
        onActivated: {
            if (backend.calibrationStep === Backend.Done)
                backend.closeCalibration()
            else if (win.calibrating)
                backend.cancelCalibration()
            else
                win.helpVisible = false
        }
    }

    component Readout: ColumnLayout {
        property string label
        property string value
        property bool big: false

        spacing: 0
        Label {
            text: parent.label
            color: "#9a9aa3"
            font.pixelSize: 12
        }
        Label {
            text: parent.value
            color: "#ececef"
            font.pixelSize: parent.big ? 40 : 22
            font.bold: parent.big
            // Digits of one width, so a changing number does not shake the row.
            font.features: { "tnum": 1 }
        }
    }

    component PresetButton: ActionButton {
        property string name

        primary: win.hostClick && backend.feel === name
        enabled: win.hostClick
        implicitWidth: Math.max(96, contentItem.implicitWidth + 30)
        onClicked: backend.applyPreset(name)
    }

    component Key: Rectangle {
        property string text

        implicitWidth: Math.max(26, keyLabel.implicitWidth + 14)
        implicitHeight: 24
        radius: Math.min(4, theme.radius)
        color: "#232328"
        border.color: "#3a3a42"
        Label {
            id: keyLabel
            anchors.centerIn: parent
            text: parent.text
            color: "#ececef"
            font.pixelSize: 12
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        Flickable {
            id: page
            anchors.fill: parent
            contentWidth: width
            contentHeight: Math.max(height, column.implicitHeight + 40)
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: column
                x: 20
                y: 20
                width: page.width - 40
                // At least the window's height, so the chart takes what the
                // settings leave; taller, and the page scrolls.
                height: Math.max(implicitHeight, page.height - 40)
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        text: "Tactile"
                        color: "#ececef"
                        font.pixelSize: 22
                        font.bold: true
                    }
                    Switch {
                        // Who plays the click: Tactile, with everything
                        // below, or the trackpad by itself as without it.
                        checked: win.hostClick
                        enabled: backend.connected
                        focusPolicy: Qt.NoFocus
                        Layout.leftMargin: 4
                        onToggled: backend.set("hostClick", checked ? 1 : 0)
                        Accessible.name: qsTr("Tactile plays the click")
                    }
                    Label {
                        text: !backend.connected ? qsTr("Not connected")
                            : !win.hostClick ? qsTr("Off: the trackpad's firmware plays the click, as without Tactile")
                            : backend.problem === "" ? qsTr("Tactile plays the click")
                            : qsTr("The trackpad's firmware plays the click")
                        color: "#9a9aa3"
                        font.pixelSize: 14
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("Press ? for shortcuts")
                        color: "#6c6c75"
                        font.pixelSize: 12
                    }
                }

                Rectangle {
                    visible: backend.problem !== ""
                    color: "#2a2110"
                    border.color: "#5c4514"
                    radius: theme.radius
                    implicitHeight: bannerRow.implicitHeight + 20
                    Layout.fillWidth: true

                    RowLayout {
                        id: bannerRow
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                        spacing: 12

                        Label {
                            text: win.problemText
                            color: "#f5dba8"
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        ActionButton {
                            visible: win.problemCommand !== ""
                            text: qsTr("Copy command")
                            quiet: true
                            onClicked: backend.copy(win.problemCommand)
                        }
                        ActionButton {
                            visible: backend.problem === "failed"
                            text: qsTr("Try again")
                            quiet: true
                            onClicked: backend.retry()
                        }
                    }
                }

                Rectangle {
                    id: chartCard
                    color: "#17171a"
                    border.color: "#26262b"
                    radius: theme.radius
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 320
                    Layout.preferredHeight: 360

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 32

                            Readout {
                                label: qsTr("Pressure")
                                value: win.num(backend.pressure)
                                big: true
                                Layout.minimumWidth: 108
                            }
                            Readout {
                                label: qsTr("Last click")
                                value: backend.lastPeak > 0 ? win.num(backend.lastPeak) : "–"
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 6
                            }
                            Readout {
                                label: qsTr("Typical click")
                                value: backend.typical > 0 ? win.num(backend.typical) : "–"
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 6
                            }
                            Readout {
                                label: qsTr("Fingers")
                                value: win.num(backend.fingers)
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 6
                            }
                            Readout {
                                label: qsTr("Clicks")
                                value: win.num(backend.clickCount)
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 6
                            }
                            Label {
                                // How the presses compare with what a click
                                // needs: the one thing the numbers alone do
                                // not say.
                                readonly property real ratio: backend.typical / Math.max(1, backend.settings.press ?? 1)
                                visible: win.hostClick && backend.typical > 0
                                text: {
                                    translator.language
                                    return ratio >= 1.5
                                        ? qsTr("You press about %1× harder than a click needs.").arg(ratio.toLocaleString(Qt.locale(), 'f', 1))
                                        : qsTr("Your presses end close to the click pressure.")
                                }
                                color: "#9a9aa3"
                                font.pixelSize: 13
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignRight
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 8
                            }
                            Item {
                                visible: !(win.hostClick && backend.typical > 0)
                                Layout.fillWidth: true
                            }
                            ActionButton {
                                text: backend.paused ? qsTr("Resume") : qsTr("Pause")
                                quiet: true
                                Layout.alignment: Qt.AlignBottom
                                onClicked: backend.paused = !backend.paused
                            }
                        }

                        PressureChart {
                            id: plot
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            source: backend
                            accent: theme.accent
                            surface: chartCard.color
                            corner: Math.min(4, theme.radius)
                            nowLabel: qsTr("now")
                            hoverX: hover.hovered ? hover.point.position.x : -1

                            HoverHandler { id: hover }

                            // Read the size so the lines follow a resize.
                            readonly property real pressY: { width; height; return yFor(backend.settings.press ?? 0) }
                            readonly property real releaseY: { width; height; return yFor(backend.settings.release ?? 0) }
                            readonly property real forceY: { width; height; return yFor(backend.settings.force ?? 0) }
                            readonly property real handleX: width - gutter + 8

                            // The click and release lines can run a few
                            // pixels apart, so one label sits on its line and
                            // the other hangs under it; the Force Click label
                            // keeps clear of both.
                            ChartHandle {
                                id: pressHandle
                                visible: win.hostClick
                                key: "press"
                                label: qsTr("Click %1").arg(win.num(backend.settings.press ?? 0))
                                chart: plot
                                lineY: plot.pressY
                                x: plot.handleX
                                y: plot.pressY - height
                            }
                            ChartHandle {
                                visible: win.hostClick
                                key: "release"
                                label: qsTr("Release %1").arg(win.num(backend.settings.release ?? 0))
                                chart: plot
                                lineY: plot.releaseY
                                x: plot.handleX
                                y: plot.releaseY
                            }
                            ChartHandle {
                                visible: win.forceClick
                                key: "force"
                                label: qsTr("Force %1").arg(win.num(backend.settings.force ?? 0))
                                chart: plot
                                lineY: plot.forceY
                                x: plot.handleX
                                y: Math.min(plot.forceY - height, pressHandle.y - height - 2)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    PresetButton { name: "light"; text: qsTr("Light") }
                    PresetButton { name: "medium"; text: qsTr("Medium") }
                    PresetButton { name: "firm"; text: qsTr("Firm") }
                    ActionButton {
                        // Not a choice to make: it lights up once the values
                        // are no longer one of the three.
                        text: qsTr("Custom")
                        primary: win.hostClick && backend.feel === ""
                        enabled: false
                        implicitWidth: Math.max(96, contentItem.implicitWidth + 30)
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: qsTr("Calibrate")
                        enabled: backend.connected && backend.problem !== "stock"
                        onClicked: backend.startCalibration()
                    }
                    ActionButton {
                        text: qsTr("Reset to Medium")
                        subdued: true
                        onClicked: backend.reset()
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: page.width >= 1000 ? 3 : page.width >= 660 ? 2 : 1
                    columnSpacing: 12
                    rowSpacing: 12
                    uniformCellWidths: true

                    SettingsCard {
                        title: qsTr("Click")
                        note: win.hostClick ? "" : qsTr("The trackpad's firmware plays the click at its own pressure. Switch Tactile on at the top to set it here.")

                        SettingRow {
                            key: "press"
                            label: qsTr("Click pressure")
                            hint: qsTr("How hard to press for a click. A resting finger reads 30 to 60.")
                            enabled: win.hostClick
                        }
                        SettingRow {
                            key: "release"
                            label: qsTr("Release pressure")
                            hint: qsTr("The click ends when the pressure falls below this. The tick marks 10% under the click pressure, the closest that is recommended.")
                            highest: (backend.settings.press ?? 0) - 5
                            mark: backend.suggestedRelease
                            enabled: win.hostClick
                        }
                        SettingRow {
                            key: "debounce"
                            label: qsTr("Bounce filter")
                            unit: qsTr("ms")
                            hint: qsTr("How long the pressure must stay low before the click ends, and before the next one can start.")
                            enabled: win.hostClick
                        }
                        SettingRow {
                            key: "fingerExtra"
                            label: qsTr("Extra pressure per extra finger")
                            zeroText: qsTr("None")
                            hint: qsTr("The pad adds up the pressure of all fingers. Each finger beyond the first raises the click pressure by this much, so two fingers resting to scroll do not click.")
                            enabled: win.hostClick
                        }
                        SwitchRow {
                            key: "oneFinger"
                            label: qsTr("Only one finger starts a click")
                            hint: qsTr("With two or more fingers down, nothing clicks. That includes the two-finger click, which is usually the right click.")
                            enabled: win.hostClick
                        }
                    }

                    SettingsCard {
                        title: qsTr("Feedback")
                        enabled: win.hostClick
                        corner: ActionButton {
                            text: qsTr("Feel it")
                            quiet: true
                            onClicked: backend.testClick("press")
                        }

                        PulsePattern {
                            id: pattern
                            accent: theme.accent
                            readonly property int strength: (backend.settings.strength || backend.defaults.strength) ?? 0
                            groups: {
                                translator.language
                                const s = backend.settings
                                const list = [{ label: qsTr("Press"), pulses: s.pressPulses ?? 0, gap: s.pulseGap ?? 25, strength: strength }]
                                if (win.forceClick)
                                    list.push({ label: qsTr("Force Click"), pulses: s.forcePulses ?? 0, gap: s.forceGap ?? 15,
                                                strength: (s.forceStrength || backend.defaults.forceStrength) ?? 255 })
                                list.push({ label: qsTr("Release"), pulses: s.releasePulses ?? 0, gap: s.pulseGap ?? 25,
                                            strength: s.releaseStrength || strength })
                                return list
                            }
                        }
                        Label {
                            text: qsTr("One bar per pulse: its height is the strength, the spacing is the gap. The shape of a single pulse is the trackpad's own.")
                            color: parent.enabled ? "#8b8b94" : "#5c5c64"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        SettingRow {
                            id: strengthRow
                            key: "strength"
                            label: qsTr("Strength")
                            zeroMeans: backend.defaults.strength ?? 0
                            mark: backend.suggestedStrength
                            // A light click wants a gentle pulse and a firm
                            // one a strong pulse. Offered, never done unasked.
                            readonly property bool off: Math.abs(shown - backend.suggestedStrength) > 10
                            hint: off ? qsTr("The tick is what suits a click pressure of %1: about %2.").arg(win.num(backend.settings.press ?? 0)).arg(win.num(backend.suggestedStrength))
                                      : qsTr("Suits the click pressure.")
                            action: ActionButton {
                                visible: strengthRow.off
                                text: qsTr("Match")
                                quiet: true
                                implicitHeight: 24
                                onClicked: backend.matchFeedback()
                            }
                        }
                        SettingRow {
                            key: "softness"
                            label: qsTr("Softness")
                            zeroMeans: backend.defaults.softness ?? 0
                            hint: qsTr("Shapes the pulse. Change it and feel the difference.")
                        }
                        SettingRow {
                            key: "releaseStrength"
                            label: qsTr("Strength on release")
                            zeroText: qsTr("Same as the press")
                        }
                        SettingRow {
                            key: "pressPulses"
                            label: qsTr("Pulses on press")
                            zeroText: qsTr("Silent")
                        }
                        SettingRow {
                            key: "releasePulses"
                            label: qsTr("Pulses on release")
                        }
                        SettingRow {
                            key: "pulseGap"
                            label: qsTr("Gap between pulses")
                            unit: qsTr("ms")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Label {
                                text: qsTr("Play a rhythm")
                                color: parent.enabled ? "#ececef" : "#6c6c75"
                                font.pixelSize: 14
                                Layout.fillWidth: true
                            }
                            ComboBox {
                                id: tuneBox
                                model: backend.tunes
                                focusPolicy: Qt.NoFocus
                                implicitHeight: 36
                                font.pixelSize: 14
                                font.capitalization: Font.Capitalize
                                Material.roundedScale: theme.radius
                                Accessible.name: qsTr("Play a rhythm")
                            }
                            ActionButton {
                                text: qsTr("Play")
                                enabled: tuneBox.count > 0
                                onClicked: backend.playTune(tuneBox.currentText)
                            }
                        }
                    }

                    SettingsCard {
                        id: forceCard
                        // Its settings stay out of the way until it is on.
                        readonly property bool on: backend.settings.forceClick === 1
                        title: qsTr("Force Click")
                        enabled: win.hostClick
                        note: qsTr("Press harder for a second, deeper click. It can hold a key while it lasts, which you can bind to anything.")
                        corner: [
                            ActionButton {
                                text: qsTr("Feel it")
                                quiet: true
                                enabled: win.forceClick
                                onClicked: backend.testClick("force")
                            },
                            Switch {
                                checked: backend.settings.forceClick === 1
                                focusPolicy: Qt.NoFocus
                                onToggled: backend.set("forceClick", checked ? 1 : 0)
                                Accessible.name: forceCard.title
                            }
                        ]

                        SettingRow {
                            key: "force"
                            visible: forceCard.on
                            label: qsTr("Pressure")
                            enabled: win.forceClick
                        }
                        SettingRow {
                            key: "forceRelease"
                            visible: forceCard.on
                            label: qsTr("Release pressure")
                            enabled: win.forceClick
                        }
                        SettingRow {
                            key: "forceStrength"
                            visible: forceCard.on
                            label: qsTr("Strength")
                            zeroMeans: backend.defaults.forceStrength ?? 0
                            enabled: win.forceClick
                        }
                        SettingRow {
                            key: "forceSoftness"
                            visible: forceCard.on
                            label: qsTr("Softness")
                            zeroMeans: backend.defaults.forceSoftness ?? 0
                            enabled: win.forceClick
                        }
                        SettingRow {
                            key: "forcePulses"
                            visible: forceCard.on
                            label: qsTr("Pulses")
                            enabled: win.forceClick
                        }
                        SettingRow {
                            key: "forceGap"
                            visible: forceCard.on
                            label: qsTr("Gap between pulses")
                            unit: qsTr("ms")
                            enabled: win.forceClick
                        }
                        SwitchRow {
                            key: "forceWaveform"
                            visible: forceCard.on
                            label: qsTr("Deeper pulse")
                            hint: qsTr("On: the pulse the trackpad keeps for Force Click. Off: the same pulse as an ordinary click.")
                            enabled: win.forceClick
                        }
                        ColumnLayout {
                            visible: forceCard.on
                            enabled: win.forceClick
                            spacing: 0
                            Layout.fillWidth: true

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                Label {
                                    text: qsTr("Holds the key")
                                    color: parent.enabled ? "#ececef" : "#6c6c75"
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                }
                                ComboBox {
                                    // F13 to F19: keys no keyboard here has, so
                                    // nothing else is bound to them.
                                    readonly property int stored: backend.settings.forceKey ?? 0
                                    model: {
                                        translator.language
                                        const keys = [qsTr("None")]
                                        for (let f = 13; f <= 19; f++)
                                            keys.push("F" + f)
                                        return keys
                                    }
                                    currentIndex: stored >= 183 ? stored - 182 : 0
                                    focusPolicy: Qt.NoFocus
                                    implicitHeight: 36
                                    font.pixelSize: 14
                                    Material.roundedScale: theme.radius
                                    onActivated: index => backend.set("forceKey", index === 0 ? 0 : 182 + index)
                                    Accessible.name: qsTr("Holds the key")
                                }
                            }
                            Label {
                                text: backend.settings.forceKey >= 183
                                      ? qsTr("Pressed when Force Click starts and let go when it ends. Bind it in Hyprland as code:%1 to give Force Click an action.").arg(backend.settings.forceKey + 8)
                                      : qsTr("Pressed when Force Click starts and let go when it ends. Bind the key in Hyprland to give Force Click an action.")
                                color: parent.enabled ? "#8b8b94" : "#5c5c64"
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }

        // What just happened, for a few seconds.
        Rectangle {
            visible: backend.notice !== "" && !win.calibrating
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 24 }
            width: Math.min(parent.width - 48, noticeLabel.implicitWidth + 36)
            height: noticeLabel.implicitHeight + 20
            radius: theme.radius
            color: "#2a2a30"
            border.color: "#3a3a42"

            Label {
                id: noticeLabel
                anchors { fill: parent; margins: 10; leftMargin: 18; rightMargin: 18 }
                text: backend.notice
                color: "#ececef"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        // Calibration. While it measures, the driver holds the click back, so
        // the pointer cannot press a button: the keyboard runs this.
        Rectangle {
            visible: win.calibrating
            anchors.fill: parent
            color: "#e60e0e10"

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onWheel: wheel => wheel.accepted = true
            }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 520)
                height: calibration.implicitHeight + 56
                radius: theme.radius
                color: "#17171a"
                border.color: "#26262b"

                ColumnLayout {
                    id: calibration
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28 }
                    spacing: 16

                    Label {
                        text: backend.calibrationStep === Backend.Glide ? qsTr("Step 1 of 2")
                            : backend.calibrationStep === Backend.Press ? qsTr("Step 2 of 2")
                            : qsTr("Calibrated")
                        color: "#9a9aa3"
                        font.pixelSize: 12
                    }
                    Label {
                        text: backend.calibrationStep === Backend.Glide ? qsTr("Move one finger around the trackpad, as lightly as you normally do.")
                            : backend.calibrationStep === Backend.Press ? qsTr("Now press five times, each as lightly as a click should be.")
                            : qsTr("A click now takes a pressure of %1.").arg(win.num(backend.calibrationResult))
                        color: "#ececef"
                        font.pixelSize: 20
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    ProgressBar {
                        visible: backend.calibrationStep === Backend.Glide
                        value: backend.calibrationProgress
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        visible: backend.calibrationStep === Backend.Press
                        spacing: 10
                        Repeater {
                            model: 5
                            Rectangle {
                                required property int index
                                width: 18
                                height: 18
                                radius: 9
                                color: index < backend.calibrationPresses ? theme.accent : "transparent"
                                border.color: index < backend.calibrationPresses ? theme.accent : "#4a4a52"
                                border.width: 2
                            }
                        }
                        Label {
                            text: win.num(backend.pressure)
                            color: "#9a9aa3"
                            font.pixelSize: 14
                            font.features: { "tnum": 1 }
                            Layout.leftMargin: 10
                        }
                    }

                    Label {
                        text: backend.calibrationStep === Backend.Press
                              ? qsTr("You will not feel a click while this measures. Lift your finger between presses.")
                            : backend.calibrationStep === Backend.Done
                              ? qsTr("Your finger rests at about %1 while it moves, and a click starts a little under your lightest press. Try it; Ctrl+Z brings the old pressure back.").arg(win.num(backend.calibrationGlide))
                            : qsTr("This measures how heavily your finger rests, so that moving the pointer never clicks.")
                        color: "#9a9aa3"
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: 8
                        Key { text: backend.calibrationStep === Backend.Done ? "Enter" : "Esc" }
                        Label {
                            text: backend.calibrationStep === Backend.Done ? qsTr("Done") : qsTr("Cancel")
                            color: "#9a9aa3"
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }

        // Every shortcut, on "?".
        Rectangle {
            visible: win.helpVisible
            anchors.fill: parent
            color: "#e60e0e10"

            MouseArea {
                anchors.fill: parent
                onClicked: win.helpVisible = false
                onWheel: wheel => wheel.accepted = true
            }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 460)
                height: help.implicitHeight + 56
                radius: theme.radius
                color: "#17171a"
                border.color: "#26262b"

                ColumnLayout {
                    id: help
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28 }
                    spacing: 10

                    Label {
                        text: qsTr("Shortcuts")
                        color: "#ececef"
                        font.pixelSize: 18
                        font.bold: true
                        Layout.bottomMargin: 6
                    }

                    Repeater {
                        model: [
                            { keys: ["T"], what: qsTr("Tactile on or off") },
                            { keys: ["1", "2", "3"], what: qsTr("Light, Medium, Firm") },
                            { keys: ["[", "]"], what: qsTr("Lighter or firmer click") },
                            { keys: ["Space"], what: qsTr("Feel the click") },
                            { keys: ["M"], what: qsTr("Match the feedback to the click pressure") },
                            { keys: ["C"], what: qsTr("Calibrate to your presses") },
                            { keys: ["F"], what: qsTr("Force Click on or off") },
                            { keys: ["P"], what: qsTr("Pause the chart") },
                            { keys: ["R"], what: qsTr("Reset everything to Medium") },
                            { keys: ["Ctrl", "Z"], what: qsTr("Undo") },
                            { keys: ["Ctrl", "Shift", "Z"], what: qsTr("Redo") },
                            { keys: ["?"], what: qsTr("Show or hide this") },
                            { keys: ["Q"], what: qsTr("Quit") }
                        ]
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 6

                            Label {
                                text: modelData.what
                                color: "#ececef"
                                font.pixelSize: 14
                                Layout.fillWidth: true
                            }
                            Repeater {
                                model: modelData.keys
                                Key {
                                    required property string modelData
                                    text: modelData
                                }
                            }
                        }
                    }

                    Label {
                        visible: backend.kernel !== ""
                        text: qsTr("Tactile %1 on kernel %2").arg(Qt.application.version).arg(backend.kernel)
                        color: "#6c6c75"
                        font.pixelSize: 12
                        Layout.topMargin: 10
                    }
                }
            }
        }
    }
}
