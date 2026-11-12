#pragma once

#include "MediaSession.h"
#include <memory>

class QQuickWindow;

class WindowsMediaSession final : public MediaSession {
    Q_OBJECT
public:
    explicit WindowsMediaSession(QQuickWindow *window, QObject *parent = nullptr);
    ~WindowsMediaSession() override;
    bool available() const override;
    QString unavailableReason() const override;

protected:
    void update(const MediaSessionState &previous, bool discontinuity) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
