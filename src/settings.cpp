#include "settings.h"

#include <QJsonValue>
#include <algorithm>
#include <cmath>

// Pressures are the driver's own unit: the summed finger pressure the pad
// reports. A resting finger reads 30 to 60, the firmware's own click sits near
// 125, a hard press peaks around 1000.
//
// Strength and softness are bytes of the actuator's waveform. 0 means "the
// driver's default for this trackpad", which differs between models, so that
// is what a fresh install uses.
const QList<Settings::Field> &Settings::fields() {
    static const QList<Field> list{
        {"hostClick", 0, 1, 1, "haptic_press"},
        {"press", 20, 400, 60, "haptic_press_threshold"},
        {"release", 10, 380, 35, "haptic_release_threshold"},
        {"debounce", 0, 200, 40, "haptic_debounce_ms"},
        {"strength", 0, 255, 0, "haptic_strength"},
        {"softness", 0, 255, 0, "haptic_softness"},
        {"pressPulses", 0, 4, 1, "haptic_press_pulses"},
        {"releasePulses", 1, 4, 1, "haptic_release_pulses"},
        {"pulseGap", 5, 100, 25, "haptic_pulse_gap_ms"},
        {"forceClick", 0, 1, 0, nullptr},
        {"force", 150, 1500, 500, "haptic_deep_threshold"},
        {"forceRelease", 100, 1400, 350, "haptic_deep_release_threshold"},
        {"forceStrength", 0, 255, 0, "haptic_deep_strength"},
        {"forceSoftness", 0, 255, 0, "haptic_deep_softness"},
        {"forceWaveform", 0, 1, 1, "haptic_deep_waveform"},
        {"forcePulses", 1, 4, 1, "haptic_deep_pulses"},
        {"forceGap", 5, 100, 15, "haptic_deep_gap_ms"},
    };
    return list;
}

const Settings::Field *Settings::field(const QString &key) {
    for (const auto &f : fields())
        if (key == QLatin1String(f.key))
            return &f;
    return nullptr;
}

QStringList Settings::presets() { return {"light", "medium", "firm", "firmware"}; }

Settings::Settings() {
    for (const auto &f : fields())
        m_values.insert(f.key, f.fallback);
}

bool Settings::set(const QString &key, int value) {
    if (!field(key))
        return false;
    m_values.insert(key, value);
    return true;
}

void Settings::normalize() {
    for (const auto &f : fields())
        m_values.insert(f.key, std::clamp(value(f.key), f.minimum, f.maximum));

    // A click has to end below where it starts, with room for the pad's own
    // shake when the actuator fires.
    m_values.insert("release", std::min(value("release"), value("press") - 10));
    // Force Click sits clearly above the click, and lets go between the two.
    m_values.insert("force", std::max(value("force"), value("press") + 60));
    m_values.insert("forceRelease",
                    std::clamp(value("forceRelease"), value("press") + 10, value("force") - 30));
}

bool Settings::applyPreset(const QString &name) {
    struct Preset { const char *name; int host, press, release; };
    static const Preset list[] = {
        {"light", 1, 60, 35},      // about macOS's Light
        {"medium", 1, 120, 75},    // the driver's own defaults, close to the firmware's click
        {"firm", 1, 200, 130},
        {"firmware", 0, 0, 0},     // the trackpad clicks by itself, as without Tactile
    };
    for (const auto &p : list) {
        if (name != QLatin1String(p.name))
            continue;
        m_values.insert("hostClick", p.host);
        if (p.host) {
            m_values.insert("press", p.press);
            m_values.insert("release", p.release);
        }
        normalize();
        return true;
    }
    return false;
}

QString Settings::preset() const {
    for (const auto &name : presets()) {
        Settings candidate = *this;
        candidate.applyPreset(name);
        if (candidate == *this)
            return name;
    }
    return {};
}

QJsonObject Settings::toJson() const { return m_values; }

bool Settings::merge(const QJsonObject &object, QString *error) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!field(it.key()))
            continue;
        const QJsonValue v = it.value();
        const double number = v.isBool() ? (v.toBool() ? 1 : 0) : v.toDouble(NAN);
        if (std::isnan(number) || number != std::floor(number) || std::abs(number) > 100000) {
            if (error)
                *error = QStringLiteral("%1 must be a whole number").arg(it.key());
            return false;
        }
        m_values.insert(it.key(), int(number));
    }
    normalize();
    return true;
}

QList<QPair<QString, int>> Settings::parameters() const {
    QList<QPair<QString, int>> list;
    for (const auto &f : fields()) {
        const QString key = f.key;
        if (!f.parameter || key == "hostClick")
            continue;
        // The driver switches Force Click off with a threshold of 0.
        list.append({f.parameter, key == "force" && !value("forceClick") ? 0 : value(key)});
    }
    list.append({"haptic_press", value("hostClick")});
    return list;
}
