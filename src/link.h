#pragma once

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

// The connection to tactiled: one JSON object per line, each way. It keeps
// trying while the daemon is away, so the window recovers when the service
// is started or restarted under it.
class Link : public QObject {
    Q_OBJECT

public:
    // $TACTILE_SOCKET, for trying things against a daemon run by hand.
    static QString defaultSocket();

    explicit Link(const QString &socket = defaultSocket(), QObject *parent = nullptr);
    ~Link() override;

    void open();
    bool connected() const { return m_socket.state() == QLocalSocket::ConnectedState; }
    // Sends a request and returns the id its reply will carry; 0 when there
    // is no connection.
    int send(QJsonObject request);

    // One request and its reply, for commands that need no window.
    static QJsonObject call(const QJsonObject &request, QString *error, const QString &socket = defaultSocket(),
                            int timeout = 3000);

signals:
    void connectedChanged();
    void reply(int id, const QJsonObject &message);
    void state(const QJsonObject &state);
    void pressure(int pressure, int fingers, bool button);
    // The daemon is there but does not listen to this user.
    void refused();

private:
    void receive();

    QString m_path;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QTimer m_retry;
    int m_nextId = 1;
};
