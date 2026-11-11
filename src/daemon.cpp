#include "daemon.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSysInfo>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "version.h"

namespace {
// A click threshold no finger reaches: while it is set the pad neither clicks
// nor vibrates, so a calibration measures the press and not the reaction to
// the feedback.
constexpr int HoldThreshold = 60000;
constexpr int HoldSeconds = 30;
constexpr int MaxClients = 16;
constexpr int MaxRequestBytes = 16 * 1024;
}

Daemon::Daemon(const Options &options, QObject *parent)
    : QObject(parent), m_options(options), m_driver(options.module) {
    // Sliders send a stream of changes; the file is written once they settle.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(300);
    connect(&m_saveTimer, &QTimer::timeout, this, &Daemon::save);

    m_pollTimer.setInterval(3000);
    connect(&m_pollTimer, &QTimer::timeout, this, &Daemon::poll);

    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(HoldSeconds * 1000);
    connect(&m_holdTimer, &QTimer::timeout, this, [this] { hold(m_holder, false); });

    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(15000);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        m_retrying = false;
        updateWatching();
        apply();
        broadcast();
    });

    connect(&m_server, &QLocalServer::newConnection, this, &Daemon::accept);
    connect(&m_trackpad, &Trackpad::sample, this, [this](int pressure, int fingers, bool button) {
        if (m_retrying) {
            // The driver has seen a touch with the click handed back, which
            // is what clears its count of refusals. Ask again.
            m_retrying = false;
            m_retryTimer.stop();
            updateWatching();
            apply();
            broadcast();
        }
        static QElapsedTimer uptime;
        if (!uptime.isValid())
            uptime.start();
        const QJsonObject event{{"ev", "p"}, {"t", uptime.elapsed()}, {"p", pressure},
                                {"f", fingers}, {"b", button}};
        const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
            if (it.value().watching)
                it.key()->write(line);
    });
}

Daemon::~Daemon() {
    if (m_holder)
        hold(m_holder, false);
    if (m_saveTimer.isActive())
        save();
    // The connections belong to the server, which is destroyed after the
    // list of clients; their closing must not call back into what is left.
    const auto sockets = m_clients.keys();
    for (QLocalSocket *socket : sockets)
        socket->disconnect(this);
    m_clients.clear();
    m_server.close();
    if (m_kernelLog >= 0)
        ::close(m_kernelLog);
}

bool Daemon::start(QString *error) {
    m_kind = m_driver.kind();
    m_generation = m_driver.generation();
    m_trackpadPresent = m_trackpad.present();
    load();

    // The whole log first, so the state is known even when the click was
    // handed over before the daemon started; then whatever comes.
    m_kernelLog = ::open(QFile::encodeName(m_options.kernelLog).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_kernelLog >= 0) {
        m_kernelLogNotifier = std::make_unique<QSocketNotifier>(m_kernelLog, QSocketNotifier::Read);
        connect(m_kernelLogNotifier.get(), &QSocketNotifier::activated, this, &Daemon::readKernelLog);
        readKernelLog();
    }

    apply();

    QDir().mkpath(QFileInfo(m_options.socket).absolutePath());
    QLocalServer::removeServer(m_options.socket);
    // Anyone may connect; who is listened to is decided per connection.
    m_server.setSocketOptions(QLocalServer::WorldAccessOption);
    if (!m_server.listen(m_options.socket)) {
        if (error)
            *error = QStringLiteral("cannot listen on %1: %2").arg(m_options.socket, m_server.errorString());
        return false;
    }
    m_pollTimer.start();
    return true;
}

QString Daemon::ownerFrom(const QByteArray &record) {
    if (!record.contains("magicmouse "))
        return {};
    if (record.contains("click feedback is host-driven") || record.contains("already host-controlled"))
        return "host";
    if (record.contains("cannot switch the actuator to host-controlled"))
        return "failed";
    // A freshly bound trackpad clicks by itself until the driver takes over.
    if (record.contains("returned to the actuator's firmware") || record.contains("already device-controlled")
        || record.contains("left host-controlled mode") || record.contains("initialised MTP haptics"))
        return "firmware";
    return {};
}

bool Daemon::allowed(uint uid, const QString &usersDirectory) {
    if (uid == 0 || uid == uint(geteuid()))
        return true;
    // logind keeps one file per logged-in user. "active" means a session of
    // theirs is in the foreground on a seat: someone at the machine, not a
    // remote login or a service account.
    QFile file(usersDirectory + "/" + QString::number(uid));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.readAll().split('\n').contains("STATE=active");
}

void Daemon::accept() {
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        struct ucred peer {};
        socklen_t size = sizeof peer;
        const bool known = getsockopt(int(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0;
        if (!known || !allowed(peer.uid) || m_clients.size() >= MaxClients) {
            send(socket, {{"ok", false}, {"error", "not allowed"}});
            socket->flush();
            socket->disconnectFromServer();
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            continue;
        }
        m_clients.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { receive(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] { drop(socket); });
    }
}

void Daemon::drop(QLocalSocket *socket) {
    if (!m_clients.remove(socket))
        return;
    if (m_holder == socket)
        hold(socket, false);
    updateWatching();
    socket->deleteLater();
}

void Daemon::receive(QLocalSocket *socket) {
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;
    it->buffer += socket->readAll();
    for (;;) {
        const int end = it->buffer.indexOf('\n');
        if (end < 0)
            break;
        const QByteArray line = it->buffer.left(end);
        it->buffer.remove(0, end + 1);
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject())
            continue;
        const QJsonObject request = document.object();
        QJsonObject reply = handle(socket, request);
        if (request.contains("id"))
            reply.insert("id", request.value("id"));
        send(socket, reply);
        // handle() may have ended in the client being dropped.
        it = m_clients.find(socket);
        if (it == m_clients.end())
            return;
    }
    if (it->buffer.size() > MaxRequestBytes)
        socket->abort();
}

void Daemon::send(QLocalSocket *socket, const QJsonObject &message) {
    socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void Daemon::broadcast(QLocalSocket *except) {
    const QJsonObject message{{"ev", "state"}, {"state", state()}};
    m_lastBroadcast = QJsonDocument(message).toJson(QJsonDocument::Compact);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        if (it.key() != except)
            it.key()->write(m_lastBroadcast + '\n');
}

QJsonObject Daemon::handle(QLocalSocket *socket, const QJsonObject &request) {
    const QString command = request.value("cmd").toString();
    auto failure = [](const QString &error) { return QJsonObject{{"ok", false}, {"error", error}}; };
    auto success = [this] { return QJsonObject{{"ok", true}, {"state", state()}}; };

    if (command == "status")
        return success();

    if (command == "set") {
        Settings merged = m_settings;
        QString error;
        if (!merged.merge(request.value("settings").toObject(), &error))
            return failure(error);
        if (merged != m_settings) {
            m_settings = merged;
            m_saveTimer.start();
        }
        apply();
        broadcast(socket);
        return success();
    }

    if (command == "watch") {
        m_clients[socket].watching = request.value("on").toBool(true);
        updateWatching();
        return success();
    }

    if (command == "click") {
        if (m_kind != Driver::Patched)
            return failure("The kernel's own trackpad driver is loaded; it has no test click.");
        const QString error = m_trackpad.click(request.value("kind").toString("press"));
        return error.isEmpty() ? success() : failure(error);
    }

    if (command == "hold") {
        if (m_kind != Driver::Patched)
            return failure("The kernel's own trackpad driver is loaded.");
        hold(socket, request.value("on").toBool(true));
        broadcast(socket);
        return success();
    }

    if (command == "retry") {
        retry();
        return success();
    }

    return failure(QStringLiteral("unknown command \"%1\"").arg(command));
}

QString Daemon::owner() const {
    if (m_kind != Driver::Patched || !m_driver.hostClick())
        return "firmware";
    return m_loggedOwner.isEmpty() ? "host" : m_loggedOwner;
}

QJsonObject Daemon::state() const {
    // Strength and softness of 0 stand for the driver's own waveform bytes,
    // which depend on how the pad is attached. The app shows the real number.
    const bool spi = Trackpad::isSpi();
    const QJsonObject defaults{{"strength", spi ? 0x28 : 0xff}, {"softness", spi ? 0xf0 : 0x90},
                               {"forceStrength", 0xff}, {"forceSoftness", spi ? 0xd0 : 0x90}};
    return {
        {"version", TACTILE_VERSION},
        {"kernel", QSysInfo::kernelVersion()},
        {"module", Driver::kindName(m_kind)},
        {"trackpad", m_trackpadPresent},
        {"owner", owner()},
        {"holding", m_holder != nullptr},
        {"settings", m_settings.toJson()},
        {"preset", m_settings.preset()},
        {"defaults", defaults},
        {"errors", QJsonArray::fromStringList(m_errors)},
    };
}

void Daemon::load() {
    QFile file(m_options.config);
    if (file.open(QIODevice::ReadOnly)) {
        m_settings.merge(QJsonDocument::fromJson(file.readAll()).object());
        return;
    }
    // First start. A machine where the patched driver already plays the click
    // (set up by hand, before Tactile) keeps what it has; any other starts on
    // the defaults.
    if (m_driver.kind() != Driver::Patched || !m_driver.hostClick())
        return;
    QJsonObject current;
    for (const auto &field : Settings::fields()) {
        const int value = field.parameter ? m_driver.read(field.parameter) : -1;
        if (value >= 0)
            current.insert(field.key, value);
    }
    const bool force = current.value("force").toInt() > 0;
    current.insert("forceClick", force ? 1 : 0);
    if (!force)
        current.remove("force");
    m_settings.merge(current);
    save();
}

void Daemon::save() {
    m_saveTimer.stop();
    QDir().mkpath(QFileInfo(m_options.config).absolutePath());
    // Written beside the file and renamed over it, so a crash or power cut
    // leaves the old settings and never half of the new ones.
    QSaveFile file(m_options.config);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(m_settings.toJson()).toJson());
    file.commit();
}

void Daemon::apply() {
    m_errors = m_driver.apply(m_settings);
    if (m_holder && m_kind == Driver::Patched) {
        m_driver.write("haptic_press_threshold", HoldThreshold);
        m_driver.write("haptic_deep_threshold", 0);
    }
}

void Daemon::poll() {
    const Driver::Kind kind = m_driver.kind();
    const quint64 generation = m_driver.generation();
    if (kind != m_kind || generation != m_generation) {
        // The module was loaded again (a kernel update's new build, or by
        // hand) and is back on its built-in defaults.
        m_kind = kind;
        m_generation = generation;
        apply();
    }
    m_trackpadPresent = m_trackpad.present();

    const QByteArray message = QJsonDocument(QJsonObject{{"ev", "state"}, {"state", state()}})
                                   .toJson(QJsonDocument::Compact);
    if (message != m_lastBroadcast && !m_clients.isEmpty())
        broadcast();
}

void Daemon::readKernelLog() {
    // One read is one record: "priority,sequence,time,flags;text".
    char record[8192];
    bool changed = false;
    for (;;) {
        const ssize_t size = ::read(m_kernelLog, record, sizeof record);
        if (size < 0 && (errno == EINTR || errno == EPIPE))
            continue;  // EPIPE: records were overwritten before they were read
        if (size <= 0) {
            if ((size == 0 || errno != EAGAIN) && m_kernelLogNotifier) {
                // Not a kernel log; nothing to follow. This may be running
                // inside the notifier's own signal, so it is deleted later.
                m_kernelLogNotifier->setEnabled(false);
                m_kernelLogNotifier.release()->deleteLater();
            }
            break;
        }
        const QString owner = ownerFrom(QByteArray::fromRawData(record, int(size)));
        if (!owner.isEmpty() && owner != m_loggedOwner) {
            m_loggedOwner = owner;
            changed = true;
        }
    }
    if (changed && m_server.isListening())
        broadcast();
}

void Daemon::updateWatching() {
    bool any = m_retrying;
    for (const auto &client : std::as_const(m_clients))
        any = any || client.watching;
    m_trackpad.setWatching(any);
}

void Daemon::hold(QLocalSocket *socket, bool on) {
    if (on) {
        m_holder = socket;
        m_holdTimer.start();
    } else {
        if (socket != m_holder)
            return;
        m_holder = nullptr;
        m_holdTimer.stop();
    }
    apply();
    if (!on)
        broadcast(socket);
}

void Daemon::retry() {
    if (m_kind != Driver::Patched || !m_settings.value("hostClick"))
        return;
    // The driver stops asking after a few refusals and starts over once it
    // has seen the click handed back and a touch. So hand it back, wait for a
    // touch, and ask again.
    m_driver.write("haptic_press", 0);
    m_loggedOwner.clear();
    m_retrying = true;
    m_retryTimer.start();
    updateWatching();
}
