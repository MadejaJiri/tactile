#pragma once

#include <QString>
#include <QStringList>

#include "settings.h"

// The hid-magicmouse kernel module, through its parameter files in sysfs.
// Tests point it at a directory of plain files.
class Driver {
public:
    enum Kind {
        Missing,  // no hid-magicmouse loaded
        Stock,    // the kernel's own: it cannot play a single light click
        Patched,  // the one Tactile ships
    };

    explicit Driver(const QString &moduleDirectory = "/sys/module/hid_magicmouse");

    Kind kind() const;
    static QString kindName(Kind kind);

    // Changes whenever the module is loaded again, so the daemon knows its
    // parameters are back at their defaults.
    quint64 generation() const;

    int read(const QString &parameter) const;  // -1 when unreadable
    bool write(const QString &parameter, int value, QString *error = nullptr) const;

    // Writes every setting. Only the patched module is touched: the stock one
    // would vibrate twice per press with the same values.
    QStringList apply(const Settings &settings) const;

    // True when the loaded module is Tactile's but from before some of these
    // settings existed. They take effect once the installed one is loaded.
    bool outdated(const Settings &settings) const;

    // True when the driver is asked to play the click itself. Whether the
    // trackpad went along is in the kernel log, which the daemon follows.
    bool hostClick() const { return read("haptic_press") == 1; }

private:
    QString m_directory;
};
