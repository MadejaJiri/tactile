#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <linux/input.h>
#include <unistd.h>

#include "backend.h"
#include "daemon.h"
#include "driver.h"
#include "link.h"
#include "pressurechart.h"
#include "settings.h"
#include "theme.h"
#include "trackpad.h"
#include "translator.h"

namespace {

void writeFile(const QString &path, const QByteArray &content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray("<missing>");
}

// A directory that looks like /sys/module/hid_magicmouse on its built-in
// defaults. The kernel's own module lacks the options Tactile's adds.
QString fakeModule(const QString &root, bool patched) {
    const QString directory = root + "/module";
    QDir(directory).removeRecursively();
    const QList<QPair<QString, QByteArray>> stock{
        {"haptic_press", "N"}, {"haptic_press_threshold", "120"}, {"haptic_release_threshold", "75"}};
    const QList<QPair<QString, QByteArray>> added{
        {"haptic_debounce_ms", "40"},   {"haptic_strength", "0"},
        {"haptic_softness", "0"},       {"haptic_press_pulses", "1"},
        {"haptic_release_pulses", "1"}, {"haptic_pulse_gap_ms", "25"},
        {"haptic_deep_threshold", "500"}, {"haptic_deep_release_threshold", "350"},
        {"haptic_deep_strength", "0"},  {"haptic_deep_softness", "0"},
        {"haptic_deep_waveform", "Y"},  {"haptic_deep_pulses", "1"},
        {"haptic_deep_gap_ms", "15"}};
    for (const auto &[name, value] : stock + (patched ? added : QList<QPair<QString, QByteArray>>()))
        writeFile(directory + "/parameters/" + name, value + "\n");
    return directory;
}

Daemon::Options optionsIn(const QString &root, const QString &module) {
    Daemon::Options options;
    options.config = root + "/state/settings.json";
    options.socket = root + "/sock";
    options.module = module;
    options.kernelLog = root + "/kmsg";
    return options;
}

// One request and its reply, while the daemon runs in this same thread.
QJsonObject ask(Link &link, const QJsonObject &request) {
    QSignalSpy replies(&link, &Link::reply);
    if (!link.send(request) || !replies.wait(3000))
        return {};
    return replies.first().at(1).toJsonObject();
}

// One touch: the pressure rises to the peak and the finger lifts.
double press(Backend &backend, double t, int peak, double seconds = .3, bool button = false) {
    const int steps = qMax(2, int(seconds / .01));
    for (int i = 0; i < steps; ++i, t += .01)
        backend.addSample(t, i < steps / 2 ? peak * (i + 1) / (steps / 2) : peak, 1, button);
    backend.addSample(t, 0, 0, false);
    return t + .2;
}

}  // namespace

class TactileTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // No test may reach the real service.
        QVERIFY(m_nowhere.isValid());
        qputenv("TACTILE_SOCKET", (m_nowhere.path() + "/none").toUtf8());
    }

    void cleanup() {
        // The translator sets the default locale for the language it loads.
        QLocale::setDefault(QLocale::c());
        qputenv("TACTILE_SOCKET", (m_nowhere.path() + "/none").toUtf8());
    }

    // ---- Settings ---------------------------------------------------------

    void settingsStartOnTheLightPreset() {
        const Settings settings;
        QCOMPARE(settings.preset(), QString("light"));
        QCOMPARE(settings.value("hostClick"), 1);
        QCOMPARE(settings.value("press"), 60);
        QCOMPARE(settings.value("release"), 35);
        QCOMPARE(settings.value("forceClick"), 0);
    }

    void settingsClampIntoTheirRanges() {
        Settings settings;
        settings.set("press", 5);
        settings.set("pressPulses", 9);
        settings.set("releasePulses", 0);
        settings.set("strength", 999);
        settings.normalize();
        QCOMPARE(settings.value("press"), 20);
        QCOMPARE(settings.value("pressPulses"), 4);
        QCOMPARE(settings.value("releasePulses"), 1);
        QCOMPARE(settings.value("strength"), 255);
        QVERIFY(!settings.set("nonsense", 1));
    }

    void settingsKeepThePressuresInOrder() {
        Settings settings;
        settings.set("press", 100);
        settings.set("release", 200);
        settings.set("force", 120);
        settings.set("forceRelease", 900);
        settings.normalize();
        // A click ends below where it starts; Force Click sits above it and
        // lets go between the two.
        QCOMPARE(settings.value("release"), 90);
        QCOMPARE(settings.value("force"), 160);
        QCOMPARE(settings.value("forceRelease"), 130);

        settings.set("forceRelease", 50);
        settings.normalize();
        QCOMPARE(settings.value("forceRelease"), 110);
    }

    void presetsChangeOnlyTheClick() {
        Settings settings;
        settings.set("strength", 77);
        QVERIFY(settings.applyPreset("firm"));
        QCOMPARE(settings.value("press"), 200);
        QCOMPARE(settings.value("release"), 130);
        QCOMPARE(settings.value("strength"), 77);
        QCOMPARE(settings.preset(), QString("firm"));

        QVERIFY(settings.applyPreset("firmware"));
        QCOMPARE(settings.value("hostClick"), 0);
        QCOMPARE(settings.value("press"), 200);
        QCOMPARE(settings.preset(), QString("firmware"));

        QVERIFY(settings.applyPreset("medium"));
        settings.set("press", 77);
        QCOMPARE(settings.preset(), QString());
        QVERIFY(!settings.applyPreset("nonsense"));
    }

    void mergeTakesWholeNumbersOnly() {
        Settings settings;
        QString error;
        QVERIFY(!settings.merge({{"press", "hard"}}, &error));
        QVERIFY(error.contains("press"));
        QVERIFY(!settings.merge({{"press", 80.5}}));
        QCOMPARE(settings.value("press"), 60);

        QVERIFY(settings.merge({{"press", 90}, {"forceClick", true}, {"somethingElse", 4}}));
        QCOMPARE(settings.value("press"), 90);
        QCOMPARE(settings.value("forceClick"), 1);
        QVERIFY(!settings.toJson().contains("somethingElse"));

        Settings copy;
        QVERIFY(copy.merge(settings.toJson()));
        QVERIFY(copy == settings);
    }

    void parametersHandOverTheClickLast() {
        Settings settings;
        auto parameters = settings.parameters();
        QCOMPARE(parameters.last().first, QString("haptic_press"));
        QCOMPARE(parameters.last().second, 1);

        // Force Click is switched off in the driver by a threshold of 0.
        auto valueOf = [&](const QString &name) {
            for (const auto &[parameter, value] : parameters)
                if (parameter == name)
                    return value;
            return -1;
        };
        QCOMPARE(valueOf("haptic_deep_threshold"), 0);
        settings.set("forceClick", 1);
        parameters = settings.parameters();
        QCOMPARE(valueOf("haptic_deep_threshold"), 500);
        QCOMPARE(valueOf("haptic_press_threshold"), 60);
    }

    // ---- Driver -----------------------------------------------------------

    void driverTellsTheModulesApart() {
        QTemporaryDir dir;
        QCOMPARE(Driver(dir.path() + "/none").kind(), Driver::Missing);
        QCOMPARE(Driver(fakeModule(dir.path(), false)).kind(), Driver::Stock);
        QCOMPARE(Driver(fakeModule(dir.path(), true)).kind(), Driver::Patched);
        QCOMPARE(Driver::kindName(Driver::Patched), QString("patched"));
    }

    void driverReadsSwitchesAndNumbers() {
        QTemporaryDir dir;
        const Driver driver(fakeModule(dir.path(), true));
        QCOMPARE(driver.read("haptic_press"), 0);
        QCOMPARE(driver.read("haptic_deep_waveform"), 1);
        QCOMPARE(driver.read("haptic_press_threshold"), 120);
        QCOMPARE(driver.read("no_such_parameter"), -1);
        QVERIFY(!driver.hostClick());
    }

    void driverWritesEverySetting() {
        QTemporaryDir dir;
        const Driver driver(fakeModule(dir.path(), true));
        Settings settings;
        settings.set("strength", 90);
        settings.set("forceClick", 1);
        settings.set("force", 640);
        QVERIFY(driver.apply(settings).isEmpty());
        for (const auto &[parameter, value] : settings.parameters())
            QCOMPARE(driver.read(parameter), value);
        QVERIFY(driver.hostClick());
    }

    void driverLeavesTheStockModuleAlone() {
        QTemporaryDir dir;
        const QString module = fakeModule(dir.path(), false);
        // With the same values the kernel's own module would click twice.
        QVERIFY(Driver(module).apply(Settings()).isEmpty());
        QCOMPARE(readFile(module + "/parameters/haptic_press"), QByteArray("N"));
        QCOMPARE(readFile(module + "/parameters/haptic_press_threshold"), QByteArray("120"));
    }

    void driverReportsWhatItCouldNotWrite() {
        QTemporaryDir dir;
        const QString module = fakeModule(dir.path(), true);
        QFile::remove(module + "/parameters/haptic_strength");
        QVERIFY(QDir().mkpath(module + "/parameters/haptic_strength"));
        const QStringList errors = Driver(module).apply(Settings());
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().contains("haptic_strength"));
    }

    // ---- Pressure from the event stream -----------------------------------

    void parserSumsThePressureOfAllFingers() {
        PressureParser parser;
        QVERIFY(!parser.feed(EV_ABS, ABS_MT_SLOT, 0));
        parser.feed(EV_ABS, ABS_MT_TRACKING_ID, 5);
        parser.feed(EV_ABS, ABS_MT_POSITION_X, 1234);
        parser.feed(EV_ABS, ABS_MT_PRESSURE, 40);
        parser.feed(EV_ABS, ABS_MT_SLOT, 1);
        parser.feed(EV_ABS, ABS_MT_TRACKING_ID, 6);
        parser.feed(EV_ABS, ABS_MT_PRESSURE, 30);
        QVERIFY(parser.feed(EV_SYN, SYN_REPORT, 0));
        QCOMPARE(parser.sample().pressure, 70);
        QCOMPARE(parser.sample().fingers, 2);
        QVERIFY(!parser.sample().button);

        parser.feed(EV_KEY, BTN_LEFT, 1);
        parser.feed(EV_ABS, ABS_MT_TRACKING_ID, -1);
        QVERIFY(parser.feed(EV_SYN, SYN_REPORT, 0));
        QCOMPARE(parser.sample().pressure, 40);
        QCOMPARE(parser.sample().fingers, 1);
        QVERIFY(parser.sample().button);
    }

    void parserCountsAFingerItNeverSawLand() {
        PressureParser parser;
        parser.feed(EV_ABS, ABS_MT_PRESSURE, 55);
        parser.feed(EV_SYN, SYN_REPORT, 0);
        QCOMPARE(parser.sample().fingers, 1);
        QCOMPARE(parser.sample().pressure, 55);
    }

    void parserForgetsFingersAfterLostEvents() {
        PressureParser parser;
        parser.feed(EV_ABS, ABS_MT_TRACKING_ID, 5);
        parser.feed(EV_ABS, ABS_MT_PRESSURE, 300);
        parser.feed(EV_SYN, SYN_REPORT, 0);
        parser.feed(EV_SYN, SYN_DROPPED, 0);
        parser.feed(EV_SYN, SYN_REPORT, 0);
        QCOMPARE(parser.sample().fingers, 0);
        QCOMPARE(parser.sample().pressure, 0);
    }

    // ---- Daemon -----------------------------------------------------------

    void kernelLogSaysWhoPlaysTheClick() {
        QCOMPARE(Daemon::ownerFrom("6,900,1,-;magicmouse spi0.0: click feedback is host-driven"), QString("host"));
        QCOMPARE(Daemon::ownerFrom("6,901,2,-;magicmouse spi0.0: click feedback returned to the actuator's firmware"),
                 QString("firmware"));
        QCOMPARE(Daemon::ownerFrom("4,902,3,-;magicmouse spi0.0: cannot switch the actuator to host-controlled mode "
                                   "(-5); its firmware keeps the click feedback"),
                 QString("failed"));
        QCOMPARE(Daemon::ownerFrom("6,903,4,-;usb 1-1: click feedback is host-driven"), QString());
        QCOMPARE(Daemon::ownerFrom("6,904,5,-;magicmouse spi0.0: input registered"), QString());
    }

    void onlySomeoneAtTheMachineIsListenedTo() {
        QTemporaryDir dir;
        writeFile(dir.path() + "/4242", "# This is private data.\nNAME=someone\nSTATE=active\n");
        writeFile(dir.path() + "/4243", "NAME=remote\nSTATE=online\n");
        QVERIFY(Daemon::allowed(4242, dir.path()));
        QVERIFY(!Daemon::allowed(4243, dir.path()));
        QVERIFY(!Daemon::allowed(4244, dir.path()));
        QVERIFY(Daemon::allowed(0, dir.path()));
        QVERIFY(Daemon::allowed(uint(geteuid()), dir.path()));
    }

    void daemonAppliesSavesAndRestores() {
        QTemporaryDir dir;
        const QString module = fakeModule(dir.path(), true);
        const QString parameters = module + "/parameters/";
        writeFile(dir.path() + "/kmsg", "6,1,1,-;magicmouse spi0.0: click feedback is host-driven\n");
        const Daemon::Options options = optionsIn(dir.path(), module);

        {
            Daemon daemon(options);
            QString error;
            QVERIFY2(daemon.start(&error), qPrintable(error));
            // A first start puts the Light preset in place.
            QCOMPARE(readFile(parameters + "haptic_press_threshold"), QByteArray("60"));
            QCOMPARE(readFile(parameters + "haptic_press"), QByteArray("1"));

            Link link(options.socket);
            link.open();
            QTRY_VERIFY(link.connected());

            QJsonObject reply = ask(link, {{"cmd", "status"}});
            QVERIFY(reply.value("ok").toBool());
            QJsonObject state = reply.value("state").toObject();
            QCOMPARE(state.value("module").toString(), QString("patched"));
            QCOMPARE(state.value("owner").toString(), QString("host"));
            QCOMPARE(state.value("preset").toString(), QString("light"));

            reply = ask(link, {{"cmd", "set"}, {"settings", QJsonObject{{"press", 90}, {"forceClick", 1}}}});
            QVERIFY(reply.value("ok").toBool());
            QCOMPARE(reply.value("state").toObject().value("settings").toObject().value("press").toInt(), 90);
            QCOMPARE(readFile(parameters + "haptic_press_threshold"), QByteArray("90"));
            QCOMPARE(readFile(parameters + "haptic_deep_threshold"), QByteArray("500"));

            reply = ask(link, {{"cmd", "set"}, {"settings", QJsonObject{{"press", "hard"}}}});
            QVERIFY(!reply.value("ok").toBool(true));
            QCOMPARE(readFile(parameters + "haptic_press_threshold"), QByteArray("90"));
            QVERIFY(!ask(link, {{"cmd", "nonsense"}}).value("ok").toBool(true));

            // While the app calibrates, no press may click.
            QVERIFY(ask(link, {{"cmd", "hold"}, {"on", true}}).value("state").toObject().value("holding").toBool());
            QVERIFY(readFile(parameters + "haptic_press_threshold").toInt() > 1500);
            QCOMPARE(readFile(parameters + "haptic_deep_threshold"), QByteArray("0"));
            QVERIFY(!ask(link, {{"cmd", "hold"}, {"on", false}}).value("state").toObject().value("holding").toBool());
            QCOMPARE(readFile(parameters + "haptic_press_threshold"), QByteArray("90"));

            QTRY_VERIFY(QFile::exists(options.config));
            QCOMPARE(QJsonDocument::fromJson(readFile(options.config)).object().value("press").toInt(), 90);
        }

        // After a reboot the module is back on its defaults and the daemon
        // puts the saved click in place again.
        fakeModule(dir.path(), true);
        QCOMPARE(readFile(parameters + "haptic_press_threshold"), QByteArray("120"));
        Daemon daemon(options);
        QString error;
        QVERIFY2(daemon.start(&error), qPrintable(error));
        QCOMPARE(readFile(parameters + "haptic_press_threshold"), QByteArray("90"));
        QCOMPARE(readFile(parameters + "haptic_deep_threshold"), QByteArray("500"));
        QCOMPARE(readFile(parameters + "haptic_press"), QByteArray("1"));
    }

    void daemonReleasesTheHoldWhenTheAppGoesAway() {
        QTemporaryDir dir;
        const QString module = fakeModule(dir.path(), true);
        const Daemon::Options options = optionsIn(dir.path(), module);
        Daemon daemon(options);
        QString error;
        QVERIFY2(daemon.start(&error), qPrintable(error));
        {
            Link link(options.socket);
            link.open();
            QTRY_VERIFY(link.connected());
            QVERIFY(ask(link, {{"cmd", "hold"}, {"on", true}}).value("ok").toBool());
            QVERIFY(readFile(module + "/parameters/haptic_press_threshold").toInt() > 1500);
        }
        // A crashed window must not leave a trackpad that cannot click.
        QTRY_COMPARE(readFile(module + "/parameters/haptic_press_threshold"), QByteArray("60"));
    }

    void daemonKeepsAClickSetUpByHand() {
        QTemporaryDir dir;
        const QString module = fakeModule(dir.path(), true);
        writeFile(module + "/parameters/haptic_press", "Y\n");
        writeFile(module + "/parameters/haptic_press_threshold", "80\n");
        writeFile(module + "/parameters/haptic_release_threshold", "50\n");
        writeFile(module + "/parameters/haptic_deep_threshold", "0\n");
        const Daemon::Options options = optionsIn(dir.path(), module);
        Daemon daemon(options);
        QString error;
        QVERIFY2(daemon.start(&error), qPrintable(error));

        const QJsonObject saved = QJsonDocument::fromJson(readFile(options.config)).object();
        QCOMPARE(saved.value("press").toInt(), 80);
        QCOMPARE(saved.value("release").toInt(), 50);
        QCOMPARE(saved.value("forceClick").toInt(), 0);
        QCOMPARE(readFile(module + "/parameters/haptic_press_threshold"), QByteArray("80"));
    }

    void daemonSavesButDoesNotTouchTheStockModule() {
        QTemporaryDir dir;
        const QString module = fakeModule(dir.path(), false);
        const Daemon::Options options = optionsIn(dir.path(), module);
        Daemon daemon(options);
        QString error;
        QVERIFY2(daemon.start(&error), qPrintable(error));
        Link link(options.socket);
        link.open();
        QTRY_VERIFY(link.connected());

        const QJsonObject reply = ask(link, {{"cmd", "set"}, {"settings", QJsonObject{{"press", 90}}}});
        QVERIFY(reply.value("ok").toBool());
        QCOMPARE(reply.value("state").toObject().value("module").toString(), QString("stock"));
        QCOMPARE(reply.value("state").toObject().value("owner").toString(), QString("firmware"));
        QCOMPARE(readFile(module + "/parameters/haptic_press_threshold"), QByteArray("120"));
        QCOMPARE(readFile(module + "/parameters/haptic_press"), QByteArray("N"));
        QVERIFY(!ask(link, {{"cmd", "click"}}).value("ok").toBool(true));
    }

    // ---- Backend ----------------------------------------------------------

    void backendSaysWhenTheServiceIsMissing() {
        Backend backend;
        QVERIFY(!backend.connected());
        // Not at once: the connection gets a moment first.
        QCOMPARE(backend.problem(), QString());
        QTRY_COMPARE(backend.problem(), QString("daemon"));
    }

    void backendReachesTheDriverThroughTheDaemon() {
        QTemporaryDir dir;
        const QString module = fakeModule(dir.path(), true);
        const Daemon::Options options = optionsIn(dir.path(), module);
        Daemon daemon(options);
        QString error;
        QVERIFY2(daemon.start(&error), qPrintable(error));

        qputenv("TACTILE_SOCKET", options.socket.toUtf8());
        Backend backend;
        QTRY_VERIFY(backend.connected());
        QTRY_COMPARE(backend.module(), QString("patched"));
        QCOMPARE(backend.problem(), QString());
        QCOMPARE(backend.preset(), QString("light"));

        backend.applyPreset("firm");
        QTRY_COMPARE(readFile(module + "/parameters/haptic_press_threshold"), QByteArray("200"));
        QCOMPARE(readFile(module + "/parameters/haptic_release_threshold"), QByteArray("130"));

        backend.applyPreset("firmware");
        QTRY_COMPARE(readFile(module + "/parameters/haptic_press"), QByteArray("0"));
        QTRY_COMPARE(backend.owner(), QString("firmware"));

        backend.undo();
        QTRY_COMPARE(readFile(module + "/parameters/haptic_press"), QByteArray("1"));
    }

    void backendCountsClicksAndTheirPeaks() {
        Backend backend;
        double t = 100;
        t = press(backend, t, 150, .3, true);
        t = press(backend, t, 90, .3, true);
        t = press(backend, t, 400, .3, true);
        // A touch that never clicked is not a click.
        t = press(backend, t, 45, .3, false);

        QCOMPARE(backend.clickCount(), 3);
        QCOMPARE(backend.lastPeak(), 400);
        QCOMPARE(backend.typical(), 150);
        QCOMPARE(backend.clicks().size(), 3);
        QCOMPARE(backend.clicks().at(1).peak, 90);
        QVERIFY(backend.clicks().at(1).up > backend.clicks().at(1).down);
        QCOMPARE(backend.pressure(), 0);
        QCOMPARE(backend.fingers(), 0);
    }

    void backendKeepsCountingWhilePaused() {
        Backend backend;
        double t = press(backend, 100, 150, .3, true);
        const int shown = backend.samples().size();
        backend.setPaused(true);
        press(backend, t, 200, .3, true);
        QCOMPARE(backend.samples().size(), shown);
        QCOMPARE(backend.clicks().size(), 1);
        QCOMPARE(backend.clickCount(), 2);
        QCOMPARE(backend.lastPeak(), 200);
    }

    void backendDropsOldHistory() {
        Backend backend;
        for (int i = 0; i < 4000; ++i)
            backend.addSample(100 + i * .01, 40, 1, false);
        QVERIFY(backend.samples().first().t > 100 + 39.99 - 15);
        QVERIFY(backend.samples().size() < 1600);
    }

    void percentilePicksFromTheSortedValues() {
        QCOMPARE(Backend::percentile({}, .5), 0);
        QCOMPARE(Backend::percentile({30, 10, 20}, .5), 20);
        QCOMPARE(Backend::percentile({30, 10, 20}, 0), 10);
        QCOMPARE(Backend::percentile({30, 10, 20}, 1), 30);
        QCOMPARE(Backend::percentile({1, 2, 3, 4, 100}, .95), 100);
    }

    void calibrationLandsUnderTheLightestPress() {
        // Three quarters of the lightest press...
        QCOMPARE(Backend::calibratedPress({120, 110, 130, 100, 125}, 40), 75);
        // ...but never where a resting finger already reaches...
        QCOMPARE(Backend::calibratedPress({70, 72, 71, 70, 74}, 55), 65);
        // ...and inside what the driver takes.
        QCOMPARE(Backend::calibratedPress({20, 22, 21, 20, 24}, 5), 20);
        QCOMPARE(Backend::calibratedPress({2000, 2100, 2200, 2000, 2100}, 40), 400);
        QCOMPARE(Backend::calibratedPress({}, 40), 0);
    }

    void calibrationMeasuresAGlideAndFivePresses() {
        Backend backend(true);
        QCOMPARE(backend.settings().value("press").toInt(), 60);
        backend.startCalibration();
        QCOMPARE(backend.calibrationStep(), int(Backend::Glide));

        double t = backend.now() + 1;
        for (int i = 0; i < 450; ++i, t += .01)
            backend.addSample(t, i % 7 == 0 ? 42 : 36, 1, false);
        QCOMPARE(backend.calibrationStep(), int(Backend::Press));
        QCOMPARE(backend.calibrationGlide(), 42);

        // The finger that glided has to lift before presses count.
        backend.addSample(t, 0, 0, false);
        t += .2;
        // Neither a brush nor a touch as light as gliding is a press.
        t = press(backend, t, 300, .03);
        t = press(backend, t, 50, .3);
        QCOMPARE(backend.calibrationPresses(), 0);

        for (int peak : {120, 110, 130, 100, 125})
            t = press(backend, t, peak);
        QCOMPARE(backend.calibrationStep(), int(Backend::Done));
        QCOMPARE(backend.calibrationResult(), 75);
        QCOMPARE(backend.settings().value("press").toInt(), 75);
        QCOMPARE(backend.settings().value("release").toInt(), 45);
        QCOMPARE(backend.preset(), QString());

        backend.closeCalibration();
        QCOMPARE(backend.calibrationStep(), int(Backend::Idle));
        QVERIFY(backend.canUndo());
        backend.undo();
        QCOMPARE(backend.settings().value("press").toInt(), 60);
        QCOMPARE(backend.settings().value("release").toInt(), 35);
    }

    void aLighterClickTakesItsReleaseAlong() {
        Backend backend(true);
        backend.set("press", 120);
        QCOMPARE(backend.settings().value("press").toInt(), 120);
        QCOMPARE(backend.settings().value("release").toInt(), 70);
        // One drag of a slider is one step to undo.
        backend.set("press", 130);
        backend.set("press", 140);
        backend.undo();
        QCOMPARE(backend.settings().value("press").toInt(), 60);
        QVERIFY(!backend.canUndo());
        backend.redo();
        QCOMPARE(backend.settings().value("press").toInt(), 140);

        backend.nudge(-1);
        QCOMPARE(backend.settings().value("press").toInt(), 135);
        backend.applyPreset("firmware");
        backend.nudge(1);
        QCOMPARE(backend.settings().value("press").toInt(), 135);
    }

    // ---- Chart ------------------------------------------------------------

    void chartScaleGivesTheLightEndTheRoom() {
        QCOMPARE(PressureChart::scale(0), 0.0);
        QCOMPARE(PressureChart::scale(PressureChart::Top), 1.0);
        QCOMPARE(PressureChart::scale(99999), 1.0);
        QCOMPARE(PressureChart::scale(-5), 0.0);
        // A fifteenth of the range gets a quarter of the height.
        QVERIFY(PressureChart::scale(100) > .25);
        for (int pressure : {0, 35, 60, 120, 500, 1500})
            QVERIFY(qAbs(PressureChart::unscale(PressureChart::scale(pressure)) - pressure) < .001);
    }

    void chartMapsAHandleBackToItsPressure() {
        PressureChart chart;
        chart.setSize(QSizeF(900, 360));
        for (int pressure : {35, 60, 120, 500})
            QCOMPARE(chart.pressureAt(chart.yFor(pressure)), pressure);
        QVERIFY(chart.yFor(500) < chart.yFor(60));
    }

    // ---- Theme and language -----------------------------------------------

    void themeFallsBack() {
        QTemporaryDir dir;
        const Theme theme(dir.path(), nullptr, "/bin/false");
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        writeFile(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        const Theme theme(dir.path(), nullptr, "/bin/false");
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.path() + "/response";
        const QString command = dir.path() + "/hyprctl";
        writeFile(response, "{\"int\":0}");
        writeFile(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        writeFile(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        const Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Pause").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        writeFile(dir.path() + "/language", "pt-BR\n");
        writeFile(dir.path() + "/pt.json", R"({"Pause": "Pausa", "Reset": "Repor"})");
        writeFile(dir.path() + "/pt-BR.json", R"({"Reset": "Redefinir"})");
        const Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        // The regional catalog over the base one, and English under both.
        QCOMPARE(translator.translate("Main", "Reset"), QString("Redefinir"));
        QCOMPARE(translator.translate("Main", "Pause"), QString("Pausa"));
        QVERIFY(translator.translate("Main", "Calibrate").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        writeFile(dir.path() + "/language", "cs\n");
        writeFile(dir.path() + "/cs.json",
                  R"({"%n clicks": {"one": "%n klik", "few": "%n kliky", "other": "%n kliků"}})");
        const Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clicks", nullptr, 1), QString("%n klik"));
        QCOMPARE(translator.translate("Main", "%n clicks", nullptr, 3), QString("%n kliky"));
        QCOMPARE(translator.translate("Main", "%n clicks", nullptr, 7), QString("%n kliků"));
    }

    void czechCatalogIsBuiltIn() {
        QTemporaryDir dir;
        writeFile(dir.path() + "/language", "cs\n");
        const Translator translator(dir.path() + "/language");
        QCOMPARE(translator.language(), QString("cs"));
        QCOMPARE(translator.translate("Main", "Pause"), QString("Pozastavit"));
        QCOMPARE(translator.translate("Backend", "Copied: %1"), QString("Zkopírováno: %1"));
    }

private:
    QTemporaryDir m_nowhere;
};

QTEST_MAIN(TactileTests)
#include "tactile_tests.moc"
