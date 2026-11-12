#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

// A small value seam lets both native backends and their transport/state
// tests run without an mpv core or a scene graph.
struct MediaSessionState {
    enum class Status { Stopped, Loading, Paused, Playing };
    Status status{Status::Stopped};
    QString trackId;
    QString title;
    QString url;
    qint64 positionUs{0};
    qint64 durationUs{0};
    double rate{1.0};
    double volume{1.0};
    bool hasMedia{false};
    bool hasVideo{false};
    bool canPlay{false};
    bool canPause{false};
    bool canSeek{false};
    bool canNext{false};
    bool canPrevious{false};
};

struct MediaSessionInput {
    enum class Playback { Idle, Loading, Buffering, Playing, Paused, Seeking, Error };
    Playback playback{Playback::Idle};
    bool paused{false};
    bool eof{false};
    bool loaded{false};
    bool hasVideo{false};
    QString trackId;
    QString title;
    QString file;
    double position{0};
    double duration{0};
    double rate{1};
    double volume{100};
    bool muted{false};
    bool seekable{false};
    int playlistIndex{-1};
    int playlistCount{0};
};

MediaSessionState mediaSessionState(const MediaSessionInput &input);

class MediaSession : public QObject {
    Q_OBJECT
public:
    enum class Command { Play, Pause, TogglePause, Stop, Next, Previous, Raise };
    Q_ENUM(Command)

    explicit MediaSession(QObject *parent = nullptr);
    ~MediaSession() override = default;
    void publish(const MediaSessionState &state);
    const MediaSessionState &state() const { return m_state; }
    virtual bool available() const = 0;
    virtual QString unavailableReason() const = 0;

    // Native callbacks enter on the GUI thread and are validated against the
    // latest state, including events queued immediately before stop/EOF/load.
    void request(Command command);
    void requestSeek(qint64 positionUs, const QString &trackId = {});
    void requestSeekRelative(qint64 offsetUs);
    void requestVolume(double volume);
    void requestRate(double rate);

signals:
    void commandRequested(MediaSession::Command command);
    void seekRequested(double seconds);
    void volumeRequested(double volume);
    void rateRequested(double rate);

protected:
    virtual void update(const MediaSessionState &previous, bool discontinuity) = 0;

private:
    MediaSessionState m_state;
    QElapsedTimer m_positionClock;
};
