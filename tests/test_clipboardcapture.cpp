#include "core/ClipboardCapture.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <mpv/client.h>

namespace {

using Mpv = std::unique_ptr<mpv_handle, decltype(&mpv_terminate_destroy)>;

// Stack-backed event data deliberately must NOT be passed to mpv_free_node_contents.
// Padded rows also catch accidentally treating stride as packed width * 4.
struct RawReply {
    std::vector<uchar> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255, 77, 77, 77, 77,
        0, 0, 255, 255, 255, 255, 0, 255, 77, 77, 77, 77
    };
    mpv_byte_array bytes{pixels.data(), pixels.size()};
    char widthKey[2]{"w"}, heightKey[2]{"h"}, strideKey[7]{"stride"};
    char formatKey[7]{"format"}, dataKey[5]{"data"}, format[5]{"rgba"};
    char *keys[5]{widthKey, heightKey, strideKey, formatKey, dataKey};
    mpv_node values[5]{};
    mpv_node_list list{5, values, keys};
    mpv_event_command command{};

    RawReply()
    {
        for (int i = 0; i < 3; ++i)
            values[i].format = MPV_FORMAT_INT64;
        values[0].u.int64 = 2;
        values[1].u.int64 = 2;
        values[2].u.int64 = 12;
        values[3].format = MPV_FORMAT_STRING;
        values[3].u.string = format;
        values[4].format = MPV_FORMAT_BYTE_ARRAY;
        values[4].u.ba = &bytes;
        command.result.format = MPV_FORMAT_NODE_MAP;
        command.result.u.list = &list;
    }

    mpv_event event(quint64 id)
    {
        return {MPV_EVENT_COMMAND_REPLY, 0, id, &command};
    }
};

} // namespace

class TestClipboardCapture : public QObject {
    Q_OBJECT

    QTemporaryDir m_files;
    Mpv m_mpv{nullptr, mpv_terminate_destroy};
    quint64 m_nextId = 0;
    int m_handled = 0;
    QString m_red, m_green, m_blue, m_large, m_missing;
    const QColor m_sentinel{71, 73, 79};

    auto nextId()
    {
        return [this] { return m_nextId++; };
    }

    void pump(QPointer<ClipboardCapture> capture)
    {
        for (;;) {
            const mpv_event *event = mpv_wait_event(m_mpv.get(), 0);
            if (event->event_id == MPV_EVENT_NONE)
                break;
            if (capture && capture->handleEvent(*event))
                ++m_handled;
        }
    }

    bool waitFor(QPointer<ClipboardCapture> capture, const std::function<bool()> &done)
    {
        QElapsedTimer deadline;
        deadline.start();
        while (!done() && deadline.elapsed() < 5000) {
            pump(capture);
            QTest::qWait(1);
        }
        return done();
    }

    bool loadImage(const QString &path)
    {
        const QByteArray file = path.toUtf8();
        const char *args[] = {"loadfile", file.constData(), "replace", nullptr};
        if (mpv_command(m_mpv.get(), args) < 0)
            return false;
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 5000) {
            const mpv_event *event = mpv_wait_event(m_mpv.get(), 0.01);
            // FILE_LOADED is too early: a decoded frame must be ready.
            if (event->event_id == MPV_EVENT_PLAYBACK_RESTART)
                return true;
            if (event->event_id == MPV_EVENT_END_FILE)
                return false;
        }
        return false;
    }

    QColor clipboardColor() const
    {
        return QGuiApplication::clipboard()->image().pixelColor(0, 0);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_files.isValid());
        m_red = m_files.filePath(QStringLiteral("red.png"));
        m_green = m_files.filePath(QStringLiteral("green.png"));
        m_blue = m_files.filePath(QStringLiteral("blue \u03bb.png"));
        m_large = m_files.filePath(QStringLiteral("large.png"));
        m_missing = m_files.filePath(QStringLiteral("missing.png"));
        QImage image(96, 64, QImage::Format_RGBA8888);
        for (const auto &entry : {std::pair{m_red, Qt::red}, std::pair{m_green, Qt::green},
                                  std::pair{m_blue, Qt::blue}}) {
            image.fill(entry.second);
            QVERIFY(image.save(entry.first));
        }
        QImage large(4096, 2048, QImage::Format_RGBA8888);
        large.fill(Qt::red);
        QVERIFY(large.save(m_large));
    }

    void init()
    {
        m_nextId = 0; // Explicitly exercise a valid reply ID of zero.
        m_handled = 0;
        m_mpv.reset(mpv_create());
        QVERIFY(m_mpv);
        for (const auto &option : {
                 std::pair{"config", "no"}, std::pair{"load-scripts", "no"},
                 std::pair{"terminal", "no"}, std::pair{"vo", "null"},
                 std::pair{"ao", "null"}, std::pair{"audio", "no"},
                 std::pair{"hwdec", "no"}, std::pair{"pause", "yes"},
                 std::pair{"idle", "yes"}, std::pair{"keep-open", "yes"},
                 std::pair{"osc", "no"}}) {
            QVERIFY(mpv_set_option_string(m_mpv.get(), option.first, option.second) >= 0);
        }
        QVERIFY(mpv_initialize(m_mpv.get()) >= 0);
        QImage image(4, 4, QImage::Format_RGBA8888);
        image.fill(m_sentinel);
        QGuiApplication::clipboard()->setImage(image);
    }

    void cleanup()
    {
        // Deliver any remaining receiver-bound callbacks after local helpers
        // have been destroyed. They must have been removed, not call freed state.
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        m_mpv.reset();
    }

    void noVideoBurstIsAcceptedAndCoalesced()
    {
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        for (int i = 0; i < 200; ++i)
            QVERIFY(capture.requestFrame(false));
        QCOMPARE(m_nextId, quint64(1));
        QCOMPARE(finished.count(), 0);
        QCOMPARE(clipboardColor(), m_sentinel);

        mpv_event unrelated{MPV_EVENT_COMMAND_REPLY, 0, 900, nullptr};
        QVERIFY(!capture.handleEvent(unrelated));
        unrelated = {MPV_EVENT_PROPERTY_CHANGE, 0, 0, nullptr};
        QVERIFY(!capture.handleEvent(unrelated));

        QVERIFY(waitFor(&capture, [&] { return finished.count() == 1; }));
        QCOMPARE(m_nextId, quint64(2));
        QCOMPARE(m_handled, 2);
        QCOMPARE(finished.first().at(0).toString(), QString());
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(clipboardColor(), m_sentinel);
    }

    void realRawPixels_data()
    {
        QTest::addColumn<bool>("subtitles");
        QTest::newRow("video") << false;
        QTest::newRow("subtitles") << true;
    }

    void realRawPixels()
    {
        QFETCH(bool, subtitles);
        QVERIFY(loadImage(m_red));
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        QImage atCompletion;
        bool onGuiThread = false;
        connect(&capture, &ClipboardCapture::finished, this, [&](const QString &, bool success) {
            onGuiThread = QThread::currentThread() == qApp->thread();
            if (success)
                atCompletion = QGuiApplication::clipboard()->image();
        });
        QVERIFY(capture.requestFrame(subtitles));
        QCOMPARE(finished.count(), 0);
        QVERIFY(waitFor(&capture, [&] { return finished.count() == 1; }));
        QVERIFY(finished.first().at(1).toBool());
        QVERIFY(onGuiThread);
        QCOMPARE(atCompletion.size(), QSize(96, 64));
        QCOMPARE(atCompletion.pixelColor(0, 0), QColor(Qt::red));
        // Consume more mpv events, invalidating its borrowed result storage.
        pump(&capture);
        mpv_wait_event(m_mpv.get(), 0);
        QCOMPARE(QGuiApplication::clipboard()->image(), atCompletion);
        QCOMPARE(atCompletion.pixelColor(95, 63), QColor(Qt::red));
    }

    void rawResultOwnsPixelsAndHonorsStride()
    {
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        QVERIFY(capture.requestFrame(false));
        RawReply reply;
        const mpv_event event = reply.event(0);
        QVERIFY(capture.handleEvent(event));
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(1).toBool());
        std::fill(reply.pixels.begin(), reply.pixels.end(), 0);
        pump(&capture); // The real (now obsolete) no-video reply is unrelated.
        const QImage image = QGuiApplication::clipboard()->image();
        QCOMPARE(image.size(), QSize(2, 2));
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(image.pixelColor(1, 0), QColor(Qt::green));
        QCOMPARE(image.pixelColor(0, 1), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(1, 1), QColor(Qt::yellow));
        QVERIFY(!capture.handleEvent(event));
    }

    void malformedRawResult_data()
    {
        QTest::addColumn<int>("fault");
        const char *names[] = {"width-overflow", "negative-height", "zero-stride", "short-stride",
                               "stride-overflow", "truncated", "null-bytes", "wrong-format",
                               "missing-keys", "missing-result", "command-error"};
        for (int i = 0; i < 11; ++i)
            QTest::newRow(names[i]) << i;
    }

    void malformedRawResult()
    {
        QFETCH(int, fault);
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        QVERIFY(capture.requestFrame(false));
        RawReply reply;
        mpv_event event = reply.event(0);
        switch (fault) {
        case 0: reply.values[0].u.int64 = std::numeric_limits<qint64>::max(); break;
        case 1: reply.values[1].u.int64 = -1; break;
        case 2: reply.values[2].u.int64 = 0; break;
        case 3: reply.values[2].u.int64 = 4; break;
        case 4: reply.values[2].u.int64 = std::numeric_limits<qint64>::max(); break;
        case 5: reply.bytes.size = 7; break;
        case 6: reply.bytes.data = nullptr; break;
        case 7: reply.format[0] = 'b'; break;
        case 8: reply.list.keys = nullptr; break;
        case 9: event.data = nullptr; break;
        case 10: event.error = MPV_ERROR_COMMAND; break;
        }
        QVERIFY(capture.handleEvent(event));
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(clipboardColor(), m_sentinel);
    }

    void enqueueFailureCompletesAsynchronously()
    {
        // A not-yet-initialized client rejects command submission immediately,
        // without ever posting MPV_EVENT_COMMAND_REPLY.
        Mpv uninitialized(mpv_create(), mpv_terminate_destroy);
        QVERIFY(uninitialized);
        ClipboardCapture capture(uninitialized.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        QVERIFY(capture.requestFrame(false));
        QCOMPARE(finished.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(clipboardColor(), m_sentinel);
    }

    void fileBurstPublishesOnlyNewest()
    {
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        bool onGuiThread = false;
        bool imagePresentAtCompletion = false;
        connect(&capture, &ClipboardCapture::finished, this, [&](const QString &, bool) {
            onGuiThread = QThread::currentThread() == qApp->thread();
            imagePresentAtCompletion = clipboardColor() == QColor(Qt::blue);
        });
        capture.requestFile(m_red);
        for (int i = 0; i < 200; ++i)
            capture.requestFile(i % 2 ? m_green : m_missing);
        capture.requestFile(m_blue);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(clipboardColor(), m_sentinel);
        QVERIFY(waitFor(&capture, [&] { return finished.count() == 1; }));
        QCOMPARE(finished.first().at(0).toString(), m_blue);
        QVERIFY(finished.first().at(1).toBool());
        QVERIFY(onGuiThread);
        QVERIFY(imagePresentAtCompletion);
        QCOMPARE(m_nextId, quint64(0));
    }

    void latestFileFailureDoesNotPublishOlderSuccess()
    {
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        capture.requestFile(m_red);
        capture.requestFile(m_missing);
        QVERIFY(waitFor(&capture, [&] { return finished.count() == 1; }));
        QCOMPARE(finished.first().at(0).toString(), m_missing);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(clipboardColor(), m_sentinel);
    }

    void repeatedPathIsStillANewIntent()
    {
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        for (int i = 0; i < 100; ++i)
            capture.requestFile(m_blue);
        QVERIFY(waitFor(&capture, [&] { return finished.count() == 1; }));
        QCOMPARE(finished.first().at(0).toString(), m_blue);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(clipboardColor(), QColor(Qt::blue));
    }

    void mixedModesReplacePendingWork_data()
    {
        QTest::addColumn<bool>("firstRaw");
        QTest::addColumn<bool>("lastRaw");
        QTest::newRow("raw-to-file") << true << false;
        QTest::newRow("raw-file-raw") << true << true;
        QTest::newRow("file-to-raw") << false << true;
        QTest::newRow("file-raw-file") << false << false;
    }

    void mixedModesReplacePendingWork()
    {
        QFETCH(bool, firstRaw);
        QFETCH(bool, lastRaw);
        QVERIFY(loadImage(m_green));
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        if (firstRaw)
            QVERIFY(capture.requestFrame(false));
        else
            capture.requestFile(m_red);
        for (int i = 0; i < 30; ++i) {
            capture.requestFile(m_red);
            QVERIFY(capture.requestFrame(false));
        }
        if (lastRaw)
            QVERIFY(capture.requestFrame(false));
        else
            capture.requestFile(m_blue);
        QCOMPARE(m_nextId, quint64(firstRaw ? 1 : 0));
        QCOMPARE(finished.count(), 0);
        QVERIFY(waitFor(&capture, [&] { return finished.count() == 1; }));
        QCOMPARE(m_nextId, quint64((firstRaw ? 1 : 0) + (lastRaw ? 1 : 0)));
        QCOMPARE(finished.first().at(0).toString(), lastRaw ? QString() : m_blue);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(clipboardColor(), QColor(lastRaw ? Qt::green : Qt::blue));
    }

    void invalidationDiscardsPendingAndAllowsReuse_data()
    {
        QTest::addColumn<bool>("raw");
        QTest::newRow("raw") << true;
        QTest::newRow("file") << false;
    }

    void invalidationDiscardsPendingAndAllowsReuse()
    {
        QFETCH(bool, raw);
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        if (raw)
            QVERIFY(capture.requestFrame(false));
        else
            capture.requestFile(m_large);
        QVERIFY(capture.requestFrame(false));
        capture.invalidate();
        capture.requestFile(m_blue);
        QVERIFY(waitFor(&capture, [&] { return finished.count() == 1; }));
        QCOMPARE(m_nextId, quint64(raw ? 1 : 0));
        QCOMPARE(finished.first().at(0).toString(), m_blue);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(clipboardColor(), QColor(Qt::blue));
    }

    void invalidatedRawReplyIsConsumedWithoutCompletion()
    {
        QVERIFY(loadImage(m_red));
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        QVERIFY(capture.requestFrame(false));
        capture.requestFile(m_blue);
        capture.invalidate();
        QVERIFY(waitFor(&capture, [&] { return m_handled == 1; }));
        QCOMPARE(finished.count(), 0);
        QCOMPARE(m_nextId, quint64(1));
        QCOMPARE(clipboardColor(), m_sentinel);
    }

    void closeRejectsWorkAndConsumesOutstandingRaw()
    {
        QVERIFY(loadImage(m_red));
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        QVERIFY(capture.requestFrame(false));
        capture.requestFile(m_blue);
        capture.close();
        capture.close();
        capture.invalidate();
        QVERIFY(!capture.requestFrame(false));
        capture.requestFile(m_red);
        QVERIFY(waitFor(&capture, [&] { return m_handled == 1; }));
        QCOMPARE(finished.count(), 0);
        QCOMPARE(m_nextId, quint64(1));
        QCOMPARE(clipboardColor(), m_sentinel);
    }

    void closeAndDestructionJoinFileWork()
    {
        int completions = 0;
        for (int i = 0; i < 8; ++i) {
            auto capture = std::make_unique<ClipboardCapture>(m_mpv.get(), nextId());
            connect(capture.get(), &ClipboardCapture::finished, this,
                    [&](const QString &, bool) { ++completions; });
            capture->requestFile(i % 2 ? m_blue : m_large);
            capture->requestFile(m_red);
            QVERIFY(capture->requestFrame(false));
            // Alternate immediate close with time for a worker completion to
            // queue, WITHOUT processing that completion on the GUI thread.
            if (i % 2)
                QThread::msleep(30);
            if (i % 3 == 0)
                capture->invalidate();
            if (i % 3 == 1) {
                capture->close();
                QCoreApplication::sendPostedEvents();
            }
            capture.reset();
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents();
        }
        QCOMPARE(completions, 0);
        QCOMPARE(m_nextId, quint64(0));
        QCOMPARE(clipboardColor(), m_sentinel);
    }

    void fileCaptureDoesNotRequireMpv()
    {
        ClipboardCapture capture(nullptr, {});
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        QVERIFY(!capture.requestFrame(false));
        capture.requestFile(m_blue);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(clipboardColor(), QColor(Qt::blue));
        ClipboardCapture noAllocator(m_mpv.get(), {});
        QVERIFY(!noAllocator.requestFrame(false));
    }

    void completionCanDeleteHelper()
    {
        QPointer<ClipboardCapture> capture = new ClipboardCapture(m_mpv.get(), nextId());
        connect(capture, &ClipboardCapture::finished, this,
                [&capture](const QString &, bool) { delete capture.data(); });
        capture->requestFile(m_blue);
        QVERIFY(waitFor(capture, [&] { return capture.isNull(); }));
        QCOMPARE(clipboardColor(), QColor(Qt::blue));
    }

    void clipboardChangeCanDeleteHelper()
    {
        QPointer<ClipboardCapture> capture = new ClipboardCapture(m_mpv.get(), nextId());
        QSignalSpy finished(capture, &ClipboardCapture::finished);
        QObject connectionContext;
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, &connectionContext,
                [&capture] { delete capture.data(); });
        QVERIFY(capture->requestFrame(false));
        RawReply reply;
        QVERIFY(capture->handleEvent(reply.event(0)));
        QVERIFY(capture.isNull());
        QCOMPARE(finished.count(), 0);
        QCOMPARE(clipboardColor(), QColor(Qt::red));
    }

    void clipboardChangeCanInvalidateCompletion()
    {
        ClipboardCapture capture(m_mpv.get(), nextId());
        QSignalSpy finished(&capture, &ClipboardCapture::finished);
        QObject connectionContext;
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, &connectionContext,
                [&capture] { capture.invalidate(); });
        QVERIFY(capture.requestFrame(false));
        RawReply reply;
        QVERIFY(capture.handleEvent(reply.event(0)));
        QCOMPARE(finished.count(), 0);
        QCOMPARE(clipboardColor(), QColor(Qt::red));
    }
};

int main(int argc, char **argv)
{
    // Keep these tests independent of a desktop session and its real clipboard.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestClipboardCapture test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_clipboardcapture.moc"
