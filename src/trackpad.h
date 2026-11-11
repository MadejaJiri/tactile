#pragma once

#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>
#include <memory>

struct input_event;

// Turns the trackpad's multi-touch event stream into the one number the
// driver clicks on: the pressure of all fingers together. Positions are read
// past and never kept.
class PressureParser {
public:
    struct Sample {
        int pressure = 0;
        int fingers = 0;
        bool button = false;
    };

    // True when the event completes a report; sample() is then current.
    bool feed(quint16 type, quint16 code, qint32 value);
    const Sample &sample() const { return m_sample; }
    void reset();

private:
    static constexpr int Slots = 16;
    struct Slot { bool active = false; int pressure = 0; };
    Slot m_slots[Slots];
    int m_slot = 0;
    bool m_button = false;
    bool m_dropped = false;
    Sample m_sample;
};

// The trackpad's event device. Opened only while someone watches.
class Trackpad : public QObject {
    Q_OBJECT

public:
    explicit Trackpad(QObject *parent = nullptr);
    ~Trackpad() override;

    // The event node of the pad hid-magicmouse drives, or empty.
    static QString find(const QString &sysfs = "/sys/class/input");
    // The driver's default waveform bytes differ by how the pad is attached.
    static bool isSpi(const QString &sysfs = "/sys/class/input");

    bool present() const { return !find().isEmpty(); }
    void setWatching(bool on);
    bool watching() const { return m_watching; }

    // Plays one waveform through the driver ("press", "release" or "force"),
    // at the softness it is set to and a percentage of its strength. Empty on
    // success.
    QString click(const QString &kind, int intensity = 100);

signals:
    void sample(int pressure, int fingers, bool button);

private:
    bool open();
    void close();
    void read();
    void eraseEffect();

    int m_fd = -1;
    int m_effect = -1;
    bool m_watching = false;
    std::unique_ptr<QSocketNotifier> m_notifier;
    QTimer m_retry, m_erase;
    PressureParser m_parser;
};
