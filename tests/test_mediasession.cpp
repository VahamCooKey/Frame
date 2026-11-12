// Portable state/transport regressions; no desktop, D-Bus or mpv required.
#include "platform/MediaSession.h"

#include <QSignalSpy>
#include <QtTest>
#include <limits>

class RecordingSession final : public MediaSession {
public:
    bool available() const override { return true; }
    QString unavailableReason() const override { return {}; }
    bool discontinuity{false};
    void update(const MediaSessionState &, bool changed) override { discontinuity = changed; }
};

class TestMediaState final : public QObject {
    Q_OBJECT
private:
    static MediaSessionInput loaded()
    {
        MediaSessionInput input;
        input.loaded = true;
        input.seekable = true;
        input.playback = MediaSessionInput::Playback::Playing;
        input.file = QStringLiteral("/tmp/episode one.mkv");
        input.title = QStringLiteral("Episode One");
        input.trackId = QStringLiteral("/org/mpris/MediaPlayer2/track/t1");
        input.duration = 120;
        input.position = 30;
        input.playlistIndex = 1;
        input.playlistCount = 3;
        return input;
    }
private slots:
    void idleAndLoadingDoNotAutoplay()
    {
        RecordingSession session;
        QSignalSpy commands(&session, &MediaSession::commandRequested);
        for (auto state : {MediaSessionInput::Playback::Idle, MediaSessionInput::Playback::Loading,
                           MediaSessionInput::Playback::Error}) {
            auto input = loaded();
            input.playback = state;
            session.publish(mediaSessionState(input));
            QVERIFY(!session.state().hasMedia);
            QVERIFY(!session.state().canPlay);
            QVERIFY(session.state().title.isEmpty());
            session.request(MediaSession::Command::Play);
            session.request(MediaSession::Command::TogglePause);
            session.request(MediaSession::Command::Next);
        }
        QCOMPARE(commands.count(), 0);
    }
    void pausedBufferingAndEof()
    {
        auto input = loaded();
        input.paused = true;
        input.playback = MediaSessionInput::Playback::Buffering;
        QCOMPARE(mediaSessionState(input).status, MediaSessionState::Status::Paused);
        input.paused = false;
        QCOMPARE(mediaSessionState(input).status, MediaSessionState::Status::Playing);
        input.eof = true;
        input.playback = MediaSessionInput::Playback::Idle;
        const auto ended = mediaSessionState(input);
        QCOMPARE(ended.status, MediaSessionState::Status::Stopped);
        QVERIFY(ended.canPlay);
        QVERIFY(!ended.canPause);
        RecordingSession session;
        QSignalSpy commands(&session, &MediaSession::commandRequested);
        session.publish(ended);
        session.request(MediaSession::Command::Pause);
        QCOMPARE(commands.count(), 0);
        session.request(MediaSession::Command::Play);
        QCOMPARE(commands.count(), 1);
    }
    void staleAndInvalidSeekIgnored()
    {
        RecordingSession session;
        session.publish(mediaSessionState(loaded()));
        QSignalSpy seeks(&session, &MediaSession::seekRequested);
        session.requestSeek(40000000, QStringLiteral("/org/mpris/MediaPlayer2/track/t0"));
        session.requestSeek(-1);
        session.requestSeek(121000000);
        QCOMPARE(seeks.count(), 0);
        session.requestSeek(40000000, session.state().trackId);
        QCOMPARE(seeks.count(), 1);
        QCOMPARE(seeks.takeFirst().at(0).toDouble(), 40.0);
        session.requestSeekRelative(std::numeric_limits<qint64>::min());
        QCOMPARE(seeks.takeFirst().at(0).toDouble(), 0.0);
        QSignalSpy commands(&session, &MediaSession::commandRequested);
        session.requestSeekRelative(std::numeric_limits<qint64>::max());
        QCOMPARE(commands.count(), 1);
        QCOMPARE(qvariant_cast<MediaSession::Command>(commands.first().at(0)), MediaSession::Command::Next);
        auto input = loaded(); input.seekable = false;
        session.publish(mediaSessionState(input));
        session.requestSeek(100);
        QCOMPARE(seeks.count(), 0);
    }
    void numericBoundsAndMetadata()
    {
        auto input = loaded();
        input.duration = std::numeric_limits<double>::infinity();
        input.position = std::numeric_limits<double>::quiet_NaN();
        input.rate = -1;
        input.volume = std::numeric_limits<double>::quiet_NaN();
        const auto state = mediaSessionState(input);
        QCOMPARE(state.durationUs, 0);
        QCOMPARE(state.positionUs, 0);
        QCOMPARE(state.rate, 1.0);
        QCOMPARE(state.volume, 0.0);
        QCOMPARE(state.url, QStringLiteral("file:///tmp/episode%20one.mkv"));
        QVERIFY(!state.canSeek);
        input.duration = std::numeric_limits<double>::max();
        const auto huge = mediaSessionState(input);
        QVERIFY(huge.durationUs <= std::numeric_limits<qint64>::max() / 10);
    }
    void requestsAreGatedAgainstLatestState()
    {
        RecordingSession session;
        auto input = loaded();
        input.playlistIndex = 0;
        input.playlistCount = 1;
        session.publish(mediaSessionState(input));
        QSignalSpy commands(&session, &MediaSession::commandRequested);
        session.request(MediaSession::Command::Play); // Already playing.
        session.request(MediaSession::Command::Next);
        session.request(MediaSession::Command::Previous);
        QCOMPARE(commands.count(), 0);
        session.request(MediaSession::Command::Pause);
        QCOMPARE(commands.count(), 1);
        QSignalSpy rates(&session, &MediaSession::rateRequested);
        session.requestRate(-1);
        session.requestRate(std::numeric_limits<double>::quiet_NaN());
        session.requestRate(9);
        QCOMPARE(rates.count(), 0);
        session.requestRate(1.25);
        QCOMPARE(rates.takeFirst().at(0).toDouble(), 1.25);
        session.publish({});
        session.request(MediaSession::Command::TogglePause);
        session.requestSeek(0);
        QCOMPARE(commands.count(), 1);
    }
    void discontinuitiesFollowActualPosition()
    {
        RecordingSession session;
        auto state = mediaSessionState(loaded());
        session.publish(state);
        QVERIFY(!session.discontinuity);
        session.publish(state);
        QVERIFY(!session.discontinuity);
        state.positionUs += 10000000;
        session.publish(state);
        QVERIFY(session.discontinuity);
        state.trackId = QStringLiteral("/org/mpris/MediaPlayer2/track/t2");
        state.positionUs = 0;
        session.publish(state);
        QVERIFY(!session.discontinuity);
    }
};
QTEST_GUILESS_MAIN(TestMediaState)
#include "test_mediasession.moc"
