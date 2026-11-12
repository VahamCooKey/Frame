#pragma once

#include <QObject>
#include <QPointer>

class MediaSession;
class PlayerController;
class QQuickWindow;

// Parent once to the app/window after Native.attach(). No QML context property
// or recurring timer is required. shutdown() is idempotent and also runs on
// aboutToQuit, window destruction and controller destruction.
class SystemMediaControls final : public QObject {
    Q_OBJECT
public:
    static SystemMediaControls *create(PlayerController *player, QQuickWindow *window,
                                      QObject *parent = nullptr);
    ~SystemMediaControls() override;
    bool available() const;
    QString unavailableReason() const;
    void shutdown();

private:
    SystemMediaControls(PlayerController *player, QQuickWindow *window, QObject *parent);
    void sync();
    QPointer<PlayerController> m_player;
    QPointer<QQuickWindow> m_window;
    MediaSession *m_session{nullptr};
    quint64 m_trackGeneration{0};
    bool m_loaded{false};
    bool m_seekable{false};
};
