#include "driver.h"

#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>

Driver::Driver(const QString &moduleDirectory) : m_directory(moduleDirectory) {}

Driver::Kind Driver::kind() const {
    if (!QFileInfo::exists(m_directory + "/parameters"))
        return Missing;
    // The debounce option exists only in the patched module.
    return QFileInfo::exists(m_directory + "/parameters/haptic_debounce_ms") ? Patched : Stock;
}

QString Driver::kindName(Kind kind) {
    return kind == Patched ? "patched" : kind == Stock ? "stock" : "missing";
}

quint64 Driver::generation() const {
    struct stat info;
    if (stat(QFile::encodeName(m_directory).constData(), &info) != 0)
        return 0;
    // sysfs hands out a new inode when the module is loaded again.
    return quint64(info.st_ino);
}

int Driver::read(const QString &parameter) const {
    QFile file(m_directory + "/parameters/" + parameter);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray text = file.readAll().trimmed();
    if (text == "Y")
        return 1;
    if (text == "N")
        return 0;
    bool ok = false;
    const int value = text.toInt(&ok);
    return ok ? value : -1;
}

bool Driver::write(const QString &parameter, int value, QString *error) const {
    // Unbuffered: sysfs takes one write per value and reports a refusal from
    // that write, not from a later close.
    QFile file(m_directory + "/parameters/" + parameter);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Unbuffered)) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(parameter, file.errorString());
        return false;
    }
    const QByteArray text = QByteArray::number(value) + "\n";
    if (file.write(text) != text.size()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(parameter, file.errorString());
        return false;
    }
    return true;
}

QStringList Driver::apply(const Settings &settings) const {
    QStringList errors;
    if (kind() != Patched)
        return errors;
    for (const auto &[parameter, value] : settings.parameters()) {
        if (read(parameter) == value)
            continue;
        QString error;
        if (!write(parameter, value, &error))
            errors << error;
    }
    return errors;
}
