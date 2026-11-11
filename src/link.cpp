#include "link.h"

#include <QJsonDocument>

QString Link::defaultSocket() {
    const QString fromEnvironment = qEnvironmentVariable("TACTILE_SOCKET");
    return fromEnvironment.isEmpty() ? QStringLiteral("/run/tactile/sock") : fromEnvironment;
}

Link::Link(const QString &socket, QObject *parent) : QObject(parent), m_path(socket) {
    m_retry.setInterval(1500);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_socket.state() == QLocalSocket::UnconnectedState)
            m_socket.connectToServer(m_path);
    });
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_buffer.clear();
        emit connectedChanged();
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, &Link::connectedChanged);
    connect(&m_socket, &QLocalSocket::readyRead, this, &Link::receive);
}

Link::~Link() {
    // A socket closes as it is destroyed and reports that; by then the rest
    // of the link is gone, and so may be whoever listens to it.
    m_socket.disconnect(this);
}

void Link::open() {
    m_socket.connectToServer(m_path);
    m_retry.start();
}

int Link::send(QJsonObject request) {
    if (!connected())
        return 0;
    const int id = m_nextId++;
    request.insert("id", id);
    m_socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    return id;
}

void Link::receive() {
    m_buffer += m_socket.readAll();
    for (;;) {
        const int end = m_buffer.indexOf('\n');
        if (end < 0)
            return;
        const QJsonObject message = QJsonDocument::fromJson(m_buffer.left(end)).object();
        m_buffer.remove(0, end + 1);

        const QString event = message.value("ev").toString();
        if (event == "p") {
            emit pressure(message.value("p").toInt(), message.value("f").toInt(), message.value("b").toBool());
            continue;
        }
        // The reply first: whoever asked can then tell its own answer from a
        // state another client caused.
        if (message.contains("id"))
            emit reply(message.value("id").toInt(), message);
        else if (event.isEmpty() && !message.value("ok").toBool(true))
            emit refused();
        if (message.contains("state"))
            emit state(message.value("state").toObject());
    }
}

QJsonObject Link::call(const QJsonObject &request, QString *error, const QString &path, int timeout) {
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QJsonObject();
    };
    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(timeout))
        return fail(QStringLiteral("Tactile's service is not running (%1). Start it with: sudo systemctl enable --now tactiled")
                        .arg(socket.errorString()));
    QJsonObject numbered = request;
    numbered.insert("id", 1);
    socket.write(QJsonDocument(numbered).toJson(QJsonDocument::Compact) + '\n');

    QByteArray buffer;
    for (;;) {
        int end;
        while ((end = buffer.indexOf('\n')) >= 0) {
            const QJsonObject message = QJsonDocument::fromJson(buffer.left(end)).object();
            buffer.remove(0, end + 1);
            // Events for other clients may arrive in between.
            if (message.contains("ev"))
                continue;
            if (!message.value("ok").toBool())
                return fail(message.value("error").toString("the service refused"));
            return message;
        }
        if (!socket.waitForReadyRead(timeout))
            return fail(QStringLiteral("Tactile's service did not answer."));
        buffer += socket.readAll();
    }
}
