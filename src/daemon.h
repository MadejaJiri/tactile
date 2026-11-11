#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <memory>

#include "driver.h"
#include "settings.h"
#include "tunes.h"
#include "trackpad.h"

// tactiled: the one privileged piece. It keeps the click settings, writes
// them to the kernel module (again after every reboot, resume, or driver
// reload), and passes the trackpad's pressure to the app while its window is
// open. It talks newline-separated JSON over a Unix socket and takes requests
// only from root or from a user sitting at the machine.
class Daemon : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString config = "/var/lib/tactile/settings.json";
        QString socket = "/run/tactile/sock";
        QString module = "/sys/module/hid_magicmouse";
        QString kernelLog = "/dev/kmsg";
    };

    explicit Daemon(const Options &options, QObject *parent = nullptr);
    ~Daemon() override;

    bool start(QString *error);

    // Who plays the click according to one kernel log record: "host",
    // "firmware", "failed", or empty when the record says nothing about it.
    static QString ownerFrom(const QByteArray &record);
    // Root, the daemon's own user, and users with an active local session.
    static bool allowed(uint uid, const QString &usersDirectory = "/run/systemd/users");

private:
    struct Client {
        QByteArray buffer;
        bool watching = false;
    };

    void accept();
    void receive(QLocalSocket *socket);
    void drop(QLocalSocket *socket);
    QJsonObject handle(QLocalSocket *socket, const QJsonObject &request);
    void send(QLocalSocket *socket, const QJsonObject &message);
    void broadcast(QLocalSocket *except = nullptr);

    QJsonObject state() const;
    QString owner() const;
    void load();
    void save();
    void apply();
    void poll();
    void readKernelLog();
    void updateWatching();
    void hold(QLocalSocket *socket, bool on);
    void retry();
    void playNote();

    Options m_options;
    Settings m_settings;
    Driver m_driver;
    Trackpad m_trackpad;
    QLocalServer m_server;
    QHash<QLocalSocket *, Client> m_clients;

    QStringList m_errors;       // what the module refused, from the last apply
    QString m_loggedOwner;      // from the kernel log; empty until it says
    quint64 m_generation = 0;
    Driver::Kind m_kind = Driver::Missing;
    bool m_trackpadPresent = false;
    QByteArray m_lastBroadcast;

    QLocalSocket *m_holder = nullptr;  // the client calibrating, if any
    bool m_retrying = false;

    int m_kernelLog = -1;
    std::unique_ptr<QSocketNotifier> m_kernelLogNotifier;
    QList<Tunes::Note> m_tune;  // what is left of the tune being played
    QTimer m_saveTimer, m_pollTimer, m_holdTimer, m_retryTimer, m_tuneTimer;
};
