#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QPointer>

class LinuxInhibitRequest;

// No subprocess or timer loop: acquire/release through asynchronous desktop
// D-Bus calls. Each playback generation owns a private bus connection so even
// an acquire reply lost at exit cannot leave an ownerless inhibitor behind.
class LinuxPlaybackInhibitor final : public QObject {
    Q_OBJECT
public:
    explicit LinuxPlaybackInhibitor(QObject *parent = nullptr);
    ~LinuxPlaybackInhibitor() override;

    void setPlaybackActive(bool playing, bool hasVideo);
    quint32 inhibitedFlags() const { return m_inhibitedFlags; }
    QString lastError() const { return m_lastError; }

signals:
    void inhibitionChanged();

private:
    void acquire();
    void setResult(quint32 flags, const QString &error);
    QPointer<LinuxInhibitRequest> m_request;
    quint32 m_requestedFlags{0};
    quint32 m_inhibitedFlags{0};
    QString m_lastError;
};
