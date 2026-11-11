#include "backend.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

#include "clock.h"

namespace {
constexpr double HistorySeconds = 14;
// Changing how the click feels plays it once, so the keyboard alone is
// enough to tune it. With a finger on the pad the next real click shows it.
const QStringList pressFeel{"strength", "softness", "pressPulses", "releasePulses", "pulseGap"};
const QStringList forceFeel{"forceStrength", "forceSoftness", "forceWaveform", "forcePulses", "forceGap"};
}

Backend::Backend(bool demo, QObject *parent) : QObject(parent), m_demo(demo) {
    m_sendTimer.setSingleShot(true);
    m_sendTimer.setInterval(25);
    connect(&m_sendTimer, &QTimer::timeout, this, &Backend::flush);

    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(350);
    connect(&m_previewTimer, &QTimer::timeout, this, [this] {
        if (m_last.fingers == 0 && m_link.connected() && m_step == Idle)
            m_link.send({{"cmd", "click"}, {"kind", m_previewKind}});
    });

    m_noticeTimer.setSingleShot(true);
    m_noticeTimer.setInterval(5000);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this] { say({}); });

    if (m_demo) {
        m_grace = false;
        m_state = {{"module", "patched"}, {"owner", "host"}, {"trackpad", true}, {"kernel", "demo"},
                   {"defaults", QJsonObject{{"strength", 40}, {"softness", 240},
                                            {"forceStrength", 255}, {"forceSoftness", 208}}}};
        m_settings.set("forceClick", 1);
        m_settings.normalize();
        // Start with a window's worth of history, so the first frame already
        // shows what the chart is for.
        m_demoStart = monotonicSeconds() - 8;
        for (double phase = 0; phase < 8; phase += .01) {
            const Sample s = demoSample(phase);
            addSample(m_demoStart + phase, s.pressure, s.fingers, s.button);
        }
        m_demoTimer.setInterval(10);
        connect(&m_demoTimer, &QTimer::timeout, this, &Backend::demoTick);
        m_demoTimer.start();
        return;
    }

    connect(&m_link, &Link::connectedChanged, this, &Backend::onConnection);
    connect(&m_link, &Link::state, this, &Backend::onState);
    connect(&m_link, &Link::reply, this, &Backend::onReply);
    connect(&m_link, &Link::refused, this, [this] {
        m_refused = true;
        emit statusChanged();
    });
    connect(&m_link, &Link::pressure, this, [this](int pressure, int fingers, bool button) {
        addSample(monotonicSeconds(), pressure, fingers, button);
    });
    QTimer::singleShot(800, this, [this] {
        m_grace = false;
        emit statusChanged();
    });
    m_link.open();
}

// ---- Status ---------------------------------------------------------------

QString Backend::problem() const {
    if (m_demo)
        return {};
    if (!m_link.connected())
        return m_grace ? QString() : m_refused ? "refused" : "daemon";
    if (m_state.isEmpty())
        return {};
    if (module() == "missing")
        return "missing";
    if (module() == "stock")
        return "stock";
    if (!m_state.value("trackpad").toBool())
        return "notrackpad";
    if (owner() == "failed")
        return "failed";
    if (!errors().isEmpty())
        return "errors";
    return {};
}

QStringList Backend::errors() const {
    QStringList list;
    for (const auto &value : m_state.value("errors").toArray())
        list << value.toString();
    return list;
}

Backend::~Backend() {
    // The link says "disconnected" as it is torn down, which is after the
    // history and the timers that answer to that are already gone.
    m_link.disconnect(this);
}

void Backend::onConnection() {
    m_pendingId = 0;
    if (m_link.connected()) {
        m_refused = false;
        m_link.send({{"cmd", "status"}});
        m_link.send({{"cmd", "watch"}, {"on", true}});
    } else {
        m_state = {};
        endCalibration();
        addSample(monotonicSeconds(), 0, 0, false);
    }
    emit statusChanged();
}

void Backend::onReply(int id, const QJsonObject &message) {
    if (id == m_pendingId)
        m_pendingId = 0;
    if (id == m_clickId) {
        m_clickId = 0;
        if (!message.value("ok").toBool())
            say(message.value("error").toString());
    }
}

void Backend::onState(const QJsonObject &state) {
    m_state = state;
    // While an edit is on its way the window is ahead of the daemon, and
    // taking its older values would make the slider jump back under the hand.
    if (!m_sendTimer.isActive() && m_pendingId == 0) {
        Settings reported;
        reported.merge(state.value("settings").toObject());
        if (reported != m_settings) {
            m_settings = reported;
            emit settingsChanged();
        }
    }
    const bool holding = state.value("holding").toBool();
    if (m_step == Glide || m_step == Press) {
        if (holding) {
            m_held = true;
        } else if (m_held) {
            // The daemon lets go by itself after a while, so a forgotten
            // calibration never leaves the trackpad without a click.
            endCalibration();
            say(tr("Calibration took too long, so it was cancelled."));
        }
    }
    emit statusChanged();
}

void Backend::say(const QString &notice) {
    if (m_notice == notice)
        return;
    m_notice = notice;
    if (notice.isEmpty())
        m_noticeTimer.stop();
    else
        m_noticeTimer.start();
    emit noticeChanged();
}

void Backend::copy(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
    say(tr("Copied: %1").arg(text));
}

// ---- Settings -------------------------------------------------------------

int Backend::minimum(const QString &key) const {
    const auto *field = Settings::field(key);
    return field ? field->minimum : 0;
}

int Backend::maximum(const QString &key) const {
    const auto *field = Settings::field(key);
    return field ? field->maximum : 0;
}

void Backend::remember(const QString &key) {
    // Dragging a slider is one change to undo, not one per pixel.
    const bool sameGesture = !key.isEmpty() && key == m_lastKey && m_lastEdit.isValid() && m_lastEdit.elapsed() < 800;
    if (!sameGesture) {
        m_undo.append(m_settings.toJson());
        if (m_undo.size() > 100)
            m_undo.removeFirst();
    }
    m_redo.clear();
    m_lastKey = key;
    m_lastEdit.restart();
    emit historyChanged();
}

void Backend::changed(const QString &key) {
    emit settingsChanged();
    if (m_demo)
        return;
    m_sendTimer.start();
    if (pressFeel.contains(key) || forceFeel.contains(key)) {
        m_previewKind = forceFeel.contains(key) ? "force" : "press";
        m_previewTimer.start();
    }
}

void Backend::flush() {
    const int id = m_link.send({{"cmd", "set"}, {"settings", m_settings.toJson()}});
    if (id)
        m_pendingId = id;
}

void Backend::set(const QString &key, int value) {
    if (!Settings::field(key) || !connected())
        return;
    Settings next = m_settings;
    // The end of a click moves with its start, and the end of a Force Click
    // with its start, so one slider is enough to make the whole click lighter
    // or firmer. The release sliders still set them apart.
    auto scaled = [&](const char *follower, const char *leader) {
        const double ratio = double(m_settings.value(follower)) / qMax(1, m_settings.value(leader));
        next.set(follower, int(std::lround(value * ratio)));
    };
    if (key == "press")
        scaled("release", "press");
    else if (key == "force")
        scaled("forceRelease", "force");
    next.set(key, value);
    next.normalize();
    if (next == m_settings)
        return;
    remember(key);
    m_settings = next;
    changed(key);
}

void Backend::toggle(const QString &key) { set(key, m_settings.value(key) ? 0 : 1); }

void Backend::applyPreset(const QString &name) {
    Settings next = m_settings;
    if (!connected() || !next.applyPreset(name) || next == m_settings)
        return;
    remember({});
    m_settings = next;
    changed({});
}

void Backend::reset() {
    if (!connected() || m_settings == Settings())
        return;
    remember({});
    m_settings = Settings();
    changed({});
}

void Backend::nudge(int direction) {
    if (m_settings.value("hostClick"))
        set("press", m_settings.value("press") + (direction < 0 ? -5 : 5));
}

void Backend::undo() {
    if (m_undo.isEmpty() || !connected())
        return;
    m_redo.append(m_settings.toJson());
    m_settings.merge(m_undo.takeLast());
    m_lastKey.clear();
    emit historyChanged();
    changed({});
}

void Backend::redo() {
    if (m_redo.isEmpty() || !connected())
        return;
    m_undo.append(m_settings.toJson());
    m_settings.merge(m_redo.takeLast());
    m_lastKey.clear();
    emit historyChanged();
    changed({});
}

void Backend::testClick(const QString &kind) {
    if (m_demo || m_step != Idle)
        return;
    const int id = m_link.send({{"cmd", "click"}, {"kind", kind}});
    if (id)
        m_clickId = id;
}

void Backend::retry() {
    m_link.send({{"cmd", "retry"}});
    say(tr("Lift your fingers, then touch the trackpad once."));
}

// ---- Pressure -------------------------------------------------------------

double Backend::now() const { return m_paused ? m_pausedAt : monotonicSeconds(); }

void Backend::setPaused(bool paused) {
    if (paused == m_paused)
        return;
    if (paused) {
        m_pausedAt = monotonicSeconds();
    } else {
        // Nothing was drawn while paused; a zero keeps the line from bridging
        // the gap at whatever height it had.
        m_samples.append({m_pausedAt, 0, 0, false});
        if (!m_clicks.isEmpty() && m_clicks.last().up == 0)
            m_clicks.last().up = m_pausedAt;
    }
    m_paused = paused;
    emit pausedChanged();
}

int Backend::percentile(QList<int> values, double fraction) {
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    return values.at(qBound(0, int(std::floor(fraction * (values.size() - 1) + .5)), int(values.size()) - 1));
}

int Backend::typical() const { return percentile(m_peaks, .5); }

void Backend::addSample(double t, int pressure, int fingers, bool button) {
    const Sample previous = m_last;
    const Sample sample{t, pressure, fingers, button};
    m_last = sample;

    if (m_step == Glide || m_step == Press)
        calibrate(sample, previous);

    if (!m_paused) {
        m_samples.append(sample);
        int stale = 0;
        while (stale < m_samples.size() - 1 && m_samples.at(stale + 1).t < t - HistorySeconds)
            ++stale;
        if (stale > 0)
            m_samples.remove(0, stale);
        while (!m_clicks.isEmpty() && m_clicks.first().up != 0 && m_clicks.first().up < t - HistorySeconds)
            m_clicks.removeFirst();

        if (button && !previous.button)
            m_clicks.append({t, 0, t, pressure});
        if (button && !m_clicks.isEmpty() && pressure > m_clicks.last().peak) {
            m_clicks.last().peak = pressure;
            m_clicks.last().peakAt = t;
        }
        if (!button && !m_clicks.isEmpty() && m_clicks.last().up == 0)
            m_clicks.last().up = t;
    }

    // The numbers beside the chart count every click, paused or not.
    if (button)
        m_runningPeak = previous.button ? qMax(m_runningPeak, pressure) : pressure;
    if (!button && previous.button) {
        m_peaks.append(m_runningPeak);
        if (m_peaks.size() > 20)
            m_peaks.removeFirst();
        ++m_clickCount;
        emit clicksChanged();
    }

    if (previous.pressure != pressure || previous.fingers != fingers || previous.button != button)
        emit liveChanged();
}

// ---- Calibration ----------------------------------------------------------

int Backend::calibratedPress(const QList<int> &peaks, int glide) {
    if (peaks.isEmpty())
        return 0;
    const int lightest = *std::min_element(peaks.begin(), peaks.end());
    const auto *field = Settings::field("press");
    return qBound(field->minimum, qMax(int(std::lround(lightest * .75)), glide + 10), field->maximum);
}

void Backend::startCalibration() {
    if (!connected() || m_step == Glide || m_step == Press)
        return;
    m_step = Glide;
    m_held = m_demo;
    m_touched = 0;
    m_glideSamples.clear();
    m_glide = 0;
    m_pressPeaks.clear();
    m_result = 0;
    m_inPress = false;
    // The driver must not click while the presses are measured, or its own
    // feedback would tell the finger how hard to press.
    m_link.send({{"cmd", "hold"}, {"on", true}});
    emit calibrationChanged();
}

void Backend::calibrate(const Sample &sample, const Sample &previous) {
    if (m_step == Glide) {
        if (sample.fingers == 0)
            return;
        m_glideSamples.append(sample.pressure);
        if (previous.fingers > 0)
            m_touched += qBound(0.0, sample.t - previous.t, .1);
        if (m_touched >= GlideSeconds) {
            m_glide = percentile(m_glideSamples, .95);
            m_step = Press;
            m_needLift = true;
            // A fresh hold, so the presses get the daemon's full patience.
            m_link.send({{"cmd", "hold"}, {"on", true}});
        }
        emit calibrationChanged();
        return;
    }

    if (m_needLift) {
        m_needLift = sample.fingers > 0;
        return;
    }
    if (sample.fingers > 0) {
        if (!m_inPress) {
            m_inPress = true;
            m_pressStart = sample.t;
            m_pressPeak = 0;
        }
        m_pressPeak = qMax(m_pressPeak, sample.pressure);
        return;
    }
    if (!m_inPress)
        return;
    m_inPress = false;
    // A brush or a touch no firmer than gliding is not a press.
    if (sample.t - m_pressStart < .06 || m_pressPeak < m_glide + 15)
        return;
    m_pressPeaks.append(m_pressPeak);
    if (m_pressPeaks.size() < CalibrationPresses) {
        emit calibrationChanged();
        return;
    }

    m_result = calibratedPress(m_pressPeaks, m_glide);
    m_link.send({{"cmd", "hold"}, {"on", false}});
    m_step = Done;
    m_held = false;
    remember({});
    m_settings.set("hostClick", 1);
    m_settings.set("press", m_result);
    m_settings.set("release", int(std::lround(m_result * .6)));
    m_settings.normalize();
    changed({});
    emit calibrationChanged();
}

void Backend::endCalibration() {
    if (m_step == Idle)
        return;
    m_step = Idle;
    m_held = false;
    emit calibrationChanged();
}

void Backend::cancelCalibration() {
    if (m_step == Glide || m_step == Press)
        m_link.send({{"cmd", "hold"}, {"on", false}});
    endCalibration();
}

void Backend::closeCalibration() {
    if (m_step == Done)
        endCalibration();
}

// ---- Demo -----------------------------------------------------------------

// Eight seconds that repeat: a glide, two light clicks, a firm press, and one
// more click. The button follows the settings the way the driver's would, so
// dragging a threshold changes which presses count.
Backend::Sample Backend::demoSample(double phase) {
    struct Push { double from, to; int peak; };
    static const Push presses[] = {{2.3, 2.9, 96}, {3.5, 4.2, 152}, {4.9, 6.3, 640}, {6.9, 7.4, 84}};
    phase = std::fmod(phase, 8.0);
    int pressure = 0, fingers = 0;
    if (phase > .3 && phase < 1.8) {
        fingers = 1;
        pressure = int(std::lround(40 + 7 * std::sin(phase * 9) + 4 * std::sin(phase * 23)));
    }
    for (const auto &press : presses) {
        if (phase < press.from || phase > press.to)
            continue;
        const double x = (phase - press.from) / (press.to - press.from);
        fingers = 1;
        pressure = int(std::lround(26 + (press.peak - 26) * std::pow(std::sin(M_PI * x), 1.4)));
    }
    if (m_settings.value("hostClick") == 0)
        m_demoButton = pressure >= (m_demoButton ? 90 : 125);
    else
        m_demoButton = pressure >= m_settings.value(m_demoButton ? "release" : "press");
    return {0, pressure, fingers, m_demoButton && m_step != Glide && m_step != Press};
}

void Backend::demoTick() {
    const double t = monotonicSeconds();
    const Sample s = demoSample(t - m_demoStart);
    if (s.fingers > 0 || m_last.fingers > 0)
        addSample(t, s.pressure, s.fingers, s.button);
}
