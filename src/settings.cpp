#include "settings.h"

#include <QJsonValue>
#include <algorithm>
#include <cmath>

// Pressures are the driver's own unit: the summed finger pressure the pad
// reports. A resting finger reads 30 to 60, the firmware's own click sits near
// 125, a hard press peaks around 1000.
//
// Strength and softness are bytes of the actuator's waveform. 0 means "the
// driver's default for this trackpad", which differs between models.
//
// The defaults are the Medium preset, with the feedback its pressure calls
// for; they were settled by feel on a MacBook Pro 14" (2021).
const QList<Settings::Field> &Settings::fields() {
    static const QList<Field> list{
        {"hostClick", 0, 1, 1, "haptic_press",
         "1: Tactile's driver plays the click. 0: the trackpad's firmware does"},
        {"press", 20, 400, 120, "haptic_press_threshold",
         "pressure at which a click starts"},
        {"release", 10, 395, 110, "haptic_release_threshold",
         "pressure at which it ends, at least 5 under press"},
        {"debounce", 0, 200, 1, "haptic_debounce_ms",
         "ms the pressure must stay low before the click ends"},
        {"fingerPercent", 0, 300, 100, "haptic_finger_extra",
         "what each finger beyond the first adds to every threshold, in percent of press"},
        {"oneFinger", 0, 1, 0, "haptic_one_finger",
         "1: a click starts only with exactly one finger on the pad"},
        {"strength", 0, 255, 105, "haptic_strength",
         "strength of the click's pulse (0: the driver's default)"},
        {"softness", 0, 255, 160, "haptic_softness",
         "second byte of the pulse's waveform (0: the driver's default)"},
        {"releaseStrength", 0, 255, 0, "haptic_release_strength",
         "strength of the pulse on release (0: same as strength)"},
        {"pressPulses", 0, 4, 1, "haptic_press_pulses",
         "pulses on press (0: silent)"},
        {"releasePulses", 1, 4, 1, "haptic_release_pulses",
         "pulses on release"},
        {"pulseGap", 5, 100, 25, "haptic_pulse_gap_ms",
         "ms between those pulses"},
        {"forceClick", 0, 1, 1, nullptr,
         "1: a harder press gives a second, deeper click"},
        {"force", 150, 1500, 500, "haptic_deep_threshold",
         "pressure at which Force Click starts"},
        {"forceRelease", 100, 1400, 350, "haptic_deep_release_threshold",
         "pressure at which it ends"},
        {"forceStrength", 0, 255, 0, "haptic_deep_strength",
         "strength of its pulse (0: the driver's default, the maximum)"},
        {"forceSoftness", 0, 255, 200, "haptic_deep_softness",
         "second byte of its waveform (0: the driver's default)"},
        {"forceWaveform", 0, 1, 1, "haptic_deep_waveform",
         "1: the trackpad's Force Click waveform. 0: the click's"},
        {"forcePulses", 1, 4, 1, "haptic_deep_pulses",
         "pulses of a Force Click"},
        {"forceGap", 5, 100, 15, "haptic_deep_gap_ms",
         "ms between those pulses"},
        {"forceKey", 0, LastForceKey, 0, "haptic_deep_keycode",
         "key held during a Force Click: 0 none, 183 (F13) to 189 (F19)"},
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

    // A click has to end below where it starts.
    m_values.insert("release", std::min(value("release"), value("press") - ReleaseGap));
    // The key Force Click holds is one of F13 to F19, or none.
    if (value("forceKey") < FirstForceKey)
        m_values.insert("forceKey", 0);
    // Force Click sits clearly above the click, and lets go between the two.
    m_values.insert("force", std::max(value("force"), value("press") + 60));
    m_values.insert("forceRelease",
                    std::clamp(value("forceRelease"), value("press") + 10, value("force") - 30));
}

namespace {
// How hard a click has to be pressed and what it should feel like belong
// together: a strong pulse under a light touch feels wrong, and a weak one
// under a firm press is hardly felt. Each preset carries both.
struct Preset { const char *name; int press, release, strength; };
const Preset Presets[] = {
    {"light", 60, 50, 65},      // about macOS's Light
    {"medium", 120, 110, 105},  // close to where the firmware's own click sits
    {"firm", 200, 130, 200},
};
constexpr int PresetSoftness = 160;
}

int Settings::suggestedStrength(int press) {
    // Straight lines through the presets, carried on past both ends.
    const int count = int(std::size(Presets));
    int i = 0;
    while (i < count - 2 && press > Presets[i + 1].press)
        i++;
    const Preset &low = Presets[i], &high = Presets[i + 1];
    const double slope = double(high.strength - low.strength) / (high.press - low.press);
    const double strength = low.strength + slope * (press - low.press);
    return std::clamp(int(std::lround(strength / 5.0)) * 5, 20, 255);
}

int Settings::fingerExtra() const {
    return int(std::lround(value("press") * value("fingerPercent") / 100.0));
}

int Settings::suggestedRelease(int press) {
    return std::max(10, std::min(press - ReleaseGap, int(std::lround(press * 0.9))));
}

bool Settings::applyPreset(const QString &name) {
    if (name == "firmware") {
        // The trackpad clicks by itself, as without Tactile.
        m_values.insert("hostClick", 0);
        return true;
    }
    for (const auto &p : Presets) {
        if (name != QLatin1String(p.name))
            continue;
        m_values.insert("hostClick", 1);
        m_values.insert("press", p.press);
        m_values.insert("release", p.release);
        m_values.insert("strength", p.strength);
        m_values.insert("softness", PresetSoftness);
        normalize();
        return true;
    }
    return false;
}

QString Settings::feel() const {
    for (const auto &p : Presets)
        if (value("press") == p.press && value("release") == p.release && value("strength") == p.strength
            && value("softness") == PresetSoftness)
            return p.name;
    return {};
}

QString Settings::preset() const { return value("hostClick") ? feel() : QStringLiteral("firmware"); }

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
        int number = value(key);
        // The driver switches Force Click off with a threshold of 0.
        if (key == "force" && !value("forceClick"))
            number = 0;
        // The driver takes a pressure; the setting is a share of the click
        // pressure, so that it follows a lighter or firmer click.
        if (key == "fingerPercent")
            number = fingerExtra();
        list.append({f.parameter, number});
    }
    list.append({"haptic_press", value("hostClick")});
    return list;
}
