#include "SingleInstance.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <memory>

namespace {
constexpr qint64 kMaxRequestBytes = 4 * 1024 * 1024; // Includes newline.
constexpr int kMaxClients = 16;
constexpr int kRequestLifetimeMs = 5000;
}

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

SingleInstance::~SingleInstance()
{
    close();
}

void SingleInstance::close()
{
    if (m_server) {
        while (QLocalSocket *client = m_server->nextPendingConnection()) {
            client->abort();
            client->deleteLater();
        }
        m_server->close();
    }
    const auto clients = m_clients;
    m_clients.clear();
    for (QLocalSocket *client : clients) {
        client->abort();
        client->deleteLater();
    }
}

QString SingleInstance::defaultServerName()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = qEnvironmentVariable("USER");
    // Local socket names become pipe or file names; keep them plain.
    user.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return QStringLiteral("frame-player-") + (user.isEmpty() ? QStringLiteral("default") : user);
}

bool SingleInstance::forwardToRunning(const QStringList &files, bool enqueue,
                                      const QString &serverName, int timeoutMs)
{
    QJsonObject request;
    request.insert(QStringLiteral("files"), QJsonArray::fromStringList(files));
    request.insert(QStringLiteral("enqueue"), enqueue);
    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (payload.size() > kMaxRequestBytes)
        return false;
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(timeoutMs))
        return false;
    socket.write(payload);
    if (!socket.waitForBytesWritten(timeoutMs))
        return false;

    // Wait for the acknowledgement so a half-dead instance (hung, or in the
    // middle of shutting down) does not swallow the file. The reply can
    // already be buffered by the time this loop starts (waitForBytesWritten
    // reads too), so check the buffer before each wait.
    QElapsedTimer clock;
    clock.start();
    while (!socket.canReadLine() && clock.elapsed() < timeoutMs) {
        const bool gotData = socket.waitForReadyRead(qMax(1, int(timeoutMs - clock.elapsed())));
        if (!gotData && socket.state() != QLocalSocket::ConnectedState)
            break;
    }
    return socket.canReadLine() && socket.readLine().trimmed() == "ok";
}

bool SingleInstance::listen(const QString &serverName)
{
    if (m_server)
        return m_server->isListening();

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    m_server->setMaxPendingConnections(kMaxClients);
    if (!m_server->listen(serverName)) {
        // A crash leaves the socket name behind on Unix; the connect attempt
        // in main() already failed, so nobody is behind it.
        QLocalServer::removeServer(serverName);
        if (!m_server->listen(serverName)) {
            qWarning("single-instance: cannot listen on %ls: %ls",
                     qUtf16Printable(serverName), qUtf16Printable(m_server->errorString()));
            delete m_server;
            m_server = nullptr;
            return false;
        }
    }

    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server->nextPendingConnection()) {
            if (client->state() == QLocalSocket::UnconnectedState || m_clients.size() >= kMaxClients) {
                client->abort();
                client->deleteLater();
                continue;
            }
            m_clients.insert(client);
            client->setReadBufferSize(kMaxRequestBytes);
            auto done = std::make_shared<bool>(false);
            auto *deadline = new QTimer(client);
            deadline->setSingleShot(true);
            deadline->setTimerType(Qt::PreciseTimer);
            auto reject = [this, client, done, deadline] {
                *done = true;
                deadline->stop();
                m_clients.remove(client);
                client->abort();
                client->deleteLater();
            };
            connect(deadline, &QTimer::timeout, this, reject);
            deadline->start(kRequestLifetimeMs);
            connect(client, &QLocalSocket::disconnected, this, [this, client, done, deadline] {
                *done = true;
                deadline->stop();
                m_clients.remove(client);
                client->deleteLater();
            });
            auto process = [this, client, done, deadline, reject] {
                if (*done || !m_clients.contains(client))
                    return;
                if (!client->canReadLine()) {
                    if (client->bytesAvailable() >= kMaxRequestBytes)
                        reject();
                    return;
                }
                // setReadBufferSize does not shrink data buffered before
                // acceptance. Bound the read itself and validate the framing
                // before trimming whitespace or parsing JSON.
                const QByteArray line = client->readLine(kMaxRequestBytes + 1);
                if (line.size() > kMaxRequestBytes || !line.endsWith('\n')) {
                    reject();
                    return;
                }
                *done = true; // Claim before emitting: handlers may run nested event loops.
                deadline->stop();
                const QJsonDocument doc = QJsonDocument::fromJson(line);
                if (doc.isObject()) {
                    const QJsonObject request = doc.object();
                    QStringList files;
                    for (const QJsonValue &value : request.value(QStringLiteral("files")).toArray())
                        files.append(value.toString());
                    const bool enqueue = request.value(QStringLiteral("enqueue")).toBool(true);
                    client->write("ok\n");
                    client->flush();
                    // Finish socket work before invoking application code:
                    // a handler can close us and process deferred deletion.
                    client->disconnectFromServer();
                    emit openRequested(files, enqueue);
                } else {
                    reject();
                }
            };
            connect(client, &QLocalSocket::readyRead, this, process);
            const QPointer<SingleInstance> guard(this);
            process(); // Data may already be buffered when accepted.
            if (!guard)
                return;
        }
    });
    return true;
}
