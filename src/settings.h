#pragma once

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// What the click feels like, as the app and the daemon both see it. Every
// value is an integer in a fixed range; switches are 0 or 1. The daemon turns
// them into parameters of the hid-magicmouse kernel module.
class Settings {
public:
    struct Field {
        const char *key;
        int minimum, maximum, fallback;
        const char *parameter;  // the module parameter it is written to
        const char *about;      // one line for `tactile keys`
    };

    static const QList<Field> &fields();
    static const Field *field(const QString &key);
    static QStringList presets();

    // The keys Force Click can hold: F13 to F19, as the kernel numbers them.
    // Layouts give F20 to F23 jobs of their own (one switches the touchpad
    // off), so those are not offered.
    static constexpr int FirstForceKey = 183, LastForceKey = 189;
    // The click ends at least this far below where it starts.
    static constexpr int ReleaseGap = 5;

    // The feedback strength that suits a click pressure, and the highest
    // release pressure that is still clear of it.
    static int suggestedStrength(int press);
    static int suggestedRelease(int press);

    Settings();  // the Medium preset

    int value(const QString &key) const { return m_values.value(key).toInt(); }
    bool set(const QString &key, int value);
    bool operator==(const Settings &other) const { return m_values == other.m_values; }
    bool operator!=(const Settings &other) const { return !(*this == other); }

    // Clamps every value into its range and keeps the pressures in order
    // (release below click, Force Click above it), so no combination can leave
    // a click that never ends or a Force Click that fires with the click.
    void normalize();

    // Light, Medium and Firm set the pressures and the feedback that goes
    // with them; "firmware" only hands the click back to the trackpad.
    bool applyPreset(const QString &name);
    // Which of Light, Medium and Firm the values match, or empty.
    QString feel() const;
    // The same, or "firmware" while the trackpad plays the click itself.
    QString preset() const;
    // The pressure each finger beyond the first adds, from fingerPercent.
    int fingerExtra() const;

    QJsonObject toJson() const;
    // Takes known keys from the object and ignores the rest; false with a
    // message when a value is not a whole number.
    bool merge(const QJsonObject &object, QString *error = nullptr);

    // Module parameters in the order they should be written: thresholds and
    // feel first, the switch that hands the click to the driver last.
    QList<QPair<QString, int>> parameters() const;

private:
    QJsonObject m_values;
};
