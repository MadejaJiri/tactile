#include "trackpad.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

// HID haptic usages the driver's force-feedback interface takes (the low
// half of page 0x0e usages; the deep click is Apple's own).
namespace {
constexpr quint16 WaveformPress = 0x1006;
constexpr quint16 WaveformRelease = 0x1007;
constexpr quint16 WaveformDeepClick = 0x2001;
}

void PressureParser::reset() {
    for (auto &slot : m_slots)
        slot = {};
    m_slot = 0;
    m_button = false;
    m_sample = {};
}

bool PressureParser::feed(quint16 type, quint16 code, qint32 value) {
    if (type == EV_ABS) {
        if (code == ABS_MT_SLOT) {
            m_slot = value >= 0 && value < Slots ? value : Slots - 1;
        } else if (code == ABS_MT_TRACKING_ID) {
            m_slots[m_slot].active = value >= 0;
            if (value < 0)
                m_slots[m_slot].pressure = 0;
        } else if (code == ABS_MT_PRESSURE) {
            // A watcher that joins mid-touch never saw the finger land.
            m_slots[m_slot].active = true;
            m_slots[m_slot].pressure = value;
        }
    } else if (type == EV_KEY && code == BTN_LEFT) {
        m_button = value != 0;
    } else if (type == EV_SYN && code == SYN_DROPPED) {
        // Events were lost; what is known about the fingers is stale.
        m_dropped = true;
    } else if (type == EV_SYN && code == SYN_REPORT) {
        if (m_dropped) {
            m_dropped = false;
            reset();
        }
        m_sample = {};
        for (const auto &slot : m_slots) {
            if (!slot.active)
                continue;
            ++m_sample.fingers;
            m_sample.pressure += slot.pressure;
        }
        m_sample.button = m_button;
        return true;
    }
    return false;
}

Trackpad::Trackpad(QObject *parent) : QObject(parent) {
    // The pad disappears for a moment when its driver is loaded again.
    m_retry.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_watching && m_fd < 0)
            open();
    });
    m_erase.setSingleShot(true);
    m_erase.setInterval(400);
    connect(&m_erase, &QTimer::timeout, this, &Trackpad::eraseEffect);
}

Trackpad::~Trackpad() {
    m_notifier.reset();
    close();
}

static bool hasBit(const QString &hexWords, int bit) {
    // sysfs prints capability bitmaps as hex words, most significant first.
    const QStringList words = hexWords.trimmed().split(' ', Qt::SkipEmptyParts);
    const int index = words.size() - 1 - bit / 64;
    if (index < 0)
        return false;
    return (words.at(index).toULongLong(nullptr, 16) >> (bit % 64)) & 1;
}

static QString readLine(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll()).trimmed() : QString();
}

static QString findDirectory(const QString &sysfs) {
    const auto entries = QDir(sysfs).entryList({"event*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &entry : entries) {
        const QString device = sysfs + "/" + entry + "/device";
        // The pad is the input device of the HID device hid-magicmouse is
        // bound to that reports per-finger pressure. Matching on the driver
        // rather than a name keeps the keyboard and everything else out.
        if (QFileInfo(QFileInfo(device + "/device/driver").canonicalFilePath()).fileName() != "magicmouse")
            continue;
        if (hasBit(readLine(device + "/capabilities/abs"), ABS_MT_PRESSURE))
            return sysfs + "/" + entry;
    }
    return {};
}

QString Trackpad::find(const QString &sysfs) {
    const QString directory = findDirectory(sysfs);
    return directory.isEmpty() ? QString() : "/dev/input/" + QFileInfo(directory).fileName();
}

bool Trackpad::isSpi(const QString &sysfs) {
    const QString directory = findDirectory(sysfs);
    return !directory.isEmpty() && readLine(directory + "/device/id/bustype").toInt(nullptr, 16) == BUS_SPI;
}

bool Trackpad::open() {
    if (m_fd >= 0)
        return true;
    const QString node = find();
    if (node.isEmpty())
        return false;
    // Read-write, because playing a waveform is a write. Nothing is grabbed:
    // the compositor keeps getting every event.
    m_fd = ::open(QFile::encodeName(node).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0)
        return false;
    m_parser.reset();
    m_notifier = std::make_unique<QSocketNotifier>(m_fd, QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, &Trackpad::read);
    return true;
}

void Trackpad::close() {
    m_erase.stop();
    if (m_notifier) {
        // This may run from inside the notifier's own signal (the device
        // went away mid-read), so it is deleted once that has returned.
        m_notifier->setEnabled(false);
        m_notifier.release()->deleteLater();
    }
    if (m_fd >= 0)
        ::close(m_fd);  // also drops any effect still uploaded
    m_fd = -1;
    m_effect = -1;
}

void Trackpad::setWatching(bool on) {
    m_watching = on;
    if (on) {
        open();
        m_retry.start();
    } else {
        m_retry.stop();
        if (m_effect < 0)
            close();
    }
}

void Trackpad::read() {
    struct input_event events[64];
    for (;;) {
        const ssize_t size = ::read(m_fd, events, sizeof events);
        if (size < 0 && errno == EINTR)
            continue;
        if (size < 0 && errno == EAGAIN)
            return;
        if (size <= 0) {
            // The device went away (driver reloaded); the retry timer finds
            // the new one.
            close();
            emit sample(0, 0, false);
            return;
        }
        if (!m_watching)
            continue;
        for (size_t i = 0; i < size_t(size) / sizeof(struct input_event); ++i) {
            if (m_parser.feed(events[i].type, events[i].code, events[i].value)) {
                const auto &s = m_parser.sample();
                emit sample(s.pressure, s.fingers, s.button);
            }
        }
    }
}

void Trackpad::eraseEffect() {
    if (m_fd >= 0 && m_effect >= 0)
        ioctl(m_fd, EVIOCRMFF, m_effect);
    m_effect = -1;
    if (!m_watching)
        close();
}

QString Trackpad::click(const QString &kind, int intensity) {
#ifdef FF_HAPTIC
    const quint16 usage = kind == "release" ? WaveformRelease : kind == "force" ? WaveformDeepClick : WaveformPress;
    if (!open())
        return QStringLiteral("The trackpad's event device cannot be opened.");
    eraseEffect();
    if (m_fd < 0 && !open())
        return QStringLiteral("The trackpad's event device cannot be opened.");

    struct ff_effect effect;
    memset(&effect, 0, sizeof effect);
    effect.type = FF_HAPTIC;
    effect.id = -1;
    effect.u.haptic.hid_usage = usage;
    effect.u.haptic.intensity = quint16(qBound(1, intensity, 100));  // percent of the configured strength
    if (ioctl(m_fd, EVIOCSFF, &effect) < 0)
        return QStringLiteral("The driver did not take the waveform (%1).").arg(QString::fromLocal8Bit(strerror(errno)));
    m_effect = effect.id;

    struct input_event play;
    memset(&play, 0, sizeof play);
    play.type = EV_FF;
    play.code = quint16(m_effect);
    play.value = 1;
    if (::write(m_fd, &play, sizeof play) != ssize_t(sizeof play)) {
        const QString error = QString::fromLocal8Bit(strerror(errno));
        eraseEffect();
        return QStringLiteral("The driver did not play the waveform (%1).").arg(error);
    }
    // Removing the effect cancels a waveform that has not started yet, so it
    // stays uploaded for a moment.
    m_erase.start();
    return {};
#else
    Q_UNUSED(kind)
    Q_UNUSED(intensity)
    return QStringLiteral("Built without haptic force-feedback support.");
#endif
}
