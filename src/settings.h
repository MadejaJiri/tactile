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
    };

    static const QList<Field> &fields();
    static const Field *field(const QString &key);
    static QStringList presets();

    Settings();  // the Light preset

    int value(const QString &key) const { return m_values.value(key).toInt(); }
    bool set(const QString &key, int value);
    bool operator==(const Settings &other) const { return m_values == other.m_values; }
    bool operator!=(const Settings &other) const { return !(*this == other); }

    // Clamps every value into its range and keeps the pressures in order
    // (release below click, Force Click above it), so no combination can leave
    // a click that never ends or a Force Click that fires with the click.
    void normalize();

    // Changes only who plays the click and where it starts and ends.
    bool applyPreset(const QString &name);
    QString preset() const;  // the preset the values match, or empty

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
