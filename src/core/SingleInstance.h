#pragma once

// One Frame window per user. A second launch (Explorer double-click, "Open
// with", the command line) hands its files to the running instance over a
// local socket and exits, instead of opening another window.
//
// Protocol: one UTF-8 JSON line, {"files": [...], "enqueue": bool}, answered
// with "ok\n". At most 4 MiB including the newline, 16 active clients, and
// five seconds from acceptance to a complete request. One request per
// connection; oversized, invalid, or expired requests are disconnected.

#include <QObject>
#include <QStringList>
#include <QSet>

class QLocalServer;
class QLocalSocket;

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    // Per-user server name; tests pass their own so they never reach a
    // real running Frame.
    static QString defaultServerName();

    // Tries to deliver the files to a running instance. True means they
    // were accepted and this process should exit. False means nobody is
    // listening (or it did not answer in time): become the instance.
    static bool forwardToRunning(const QStringList &files, bool enqueue,
                                 const QString &serverName = defaultServerName(),
                                 int timeoutMs = 1500);

    // Starts listening. A stale socket left by a crashed instance is
    // removed first. False means another process holds the name.
    bool listen(const QString &serverName = defaultServerName());
    // Stop accepting handoffs (called at shutdown, so a launch during the
    // last moments starts its own window instead of sending files to a
    // process that is about to exit).
    void close();

signals:
    // Files forwarded by a later launch. enqueue mirrors the sender's
    // --no-enqueue flag.
    void openRequested(const QStringList &files, bool enqueue);

private:
    QLocalServer *m_server{nullptr};
    QSet<QLocalSocket *> m_clients;
};
