#pragma once

#include "MediaSession.h"
#include <QDBusConnection>

class LinuxMediaSession final : public MediaSession {
    Q_OBJECT
public:
    explicit LinuxMediaSession(QObject *parent = nullptr);
    ~LinuxMediaSession() override;
    bool available() const override { return m_available; }
    QString unavailableReason() const override { return m_error; }
    QString serviceName() const { return m_service; }

protected:
    void update(const MediaSessionState &previous, bool discontinuity) override;

private:
    const QString m_connectionName;
    QDBusConnection m_bus;
    QString m_service;
    QString m_error;
    bool m_available{false};
};
