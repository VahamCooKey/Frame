#include <QtTest>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QBuffer>
#include <QDir>
#include <QUrl>
#include <QSemaphore>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <vector>
#include <climits>
#include <cstring>
#include <limits>
#include <memory>
#include <mpv/client.h>
#include "core/ThumbnailGenerator.h"

namespace {
// Owns all node storage so malformed metadata can be exercised without
// depending on a decoder or handing the converter dangling pointers.
struct RawScreenshot {
    std::vector<uchar> pixels;
    mpv_byte_array bytes{};
    char widthKey[2]{'w', '\0'}, heightKey[2]{'h', '\0'};
    char strideKey[7]{"stride"}, formatKey[7]{"format"}, dataKey[5]{"data"};
    char format[5]{"rgba"};
    char *keys[5]{widthKey, heightKey, strideKey, formatKey, dataKey};
    mpv_node values[5]{};
    mpv_node_list list{5, values, keys};
    mpv_node node{};

    RawScreenshot(int width, int height, int padding = 0)
        : pixels(size_t(width * 4 + padding) * height, 0)
    {
        bytes = {pixels.data(), pixels.size()};
        for (int i = 0; i < 3; ++i) values[i].format = MPV_FORMAT_INT64;
        values[0].u.int64 = width;
        values[1].u.int64 = height;
        values[2].u.int64 = width * 4 + padding;
        values[3].format = MPV_FORMAT_STRING;
        values[3].u.string = format;
        values[4].format = MPV_FORMAT_BYTE_ARRAY;
        values[4].u.ba = &bytes;
        node.format = MPV_FORMAT_NODE_MAP;
        node.u.list = &list;
    }
};

QString sampleFile()
{
    return QFileInfo(QStringLiteral("assets/test/sample.mkv")).absoluteFilePath();
}

// A deterministic nonpreemptible metadata operation, without relying on an
// offline drive or sleep-based decoder timing. Always release it before the
// generator is destroyed, including when a QtTest assertion returns early.
struct MetadataGate {
    std::atomic<int> calls{0};
    std::atomic<int> blockCall{1};
    std::atomic<bool> onGui{false};
    std::atomic<bool> timedOut{false};
    QSemaphore entered;
    QSemaphore release;

    ThumbnailMetadata lookup(const QString &path)
    {
        if (QThread::currentThread() == QCoreApplication::instance()->thread())
            onGui = true;
        const int call = ++calls;
        if (call == blockCall) {
            entered.release();
            if (!release.tryAcquire(1, 10000))
                timedOut = true;
        }
        return lookupThumbnailMetadata(path);
    }
};
}

class TestThumbnails : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression("QObject::.*[Tt]imer"));
    }

    void screenshotOwnsItsPixelsAndHonorsStride()
    {
        unsigned char pixels[24]{255, 0, 0, 255};
        mpv_byte_array bytes{pixels, sizeof(pixels)};
        char widthKey[] = "w", heightKey[] = "h", strideKey[] = "stride", formatKey[] = "format", dataKey[] = "data";
        char format[] = "rgba";
        char *keys[]{widthKey, heightKey, strideKey, formatKey, dataKey};
        mpv_node values[5]{};
        for (int i = 0; i < 3; ++i)
            values[i].format = MPV_FORMAT_INT64;
        values[0].u.int64 = values[1].u.int64 = 2;
        values[2].u.int64 = 12; // eight pixels bytes plus four padding bytes
        values[3].format = MPV_FORMAT_STRING;
        values[3].u.string = format;
        values[4].format = MPV_FORMAT_BYTE_ARRAY;
        values[4].u.ba = &bytes;
        mpv_node_list list{5, values, keys};
        mpv_node node{};
        node.format = MPV_FORMAT_NODE_MAP;
        node.u.list = &list;
        const QImage image = ThumbnailGenerator::imageFromScreenshotNode(node);
        QCOMPARE(image.size(), QSize(2, 2));
        pixels[0] = 0;
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::red));
        values[2].u.int64 = 7; // shorter than one complete row
        QVERIFY(ThumbnailGenerator::imageFromScreenshotNode(node).isNull());
    }

    void rejectsOversizedScreenshot()
    {
        char pixels[4]{};
        mpv_byte_array bytes{pixels, sizeof(pixels)};
        char widthKey[] = "w", heightKey[] = "h", formatKey[] = "format", dataKey[] = "data";
        char format[] = "rgba";
        char *keys[]{widthKey, heightKey, formatKey, dataKey};
        mpv_node values[4]{};
        values[0].format = values[1].format = MPV_FORMAT_INT64;
        values[0].u.int64 = std::numeric_limits<int>::max();
        values[1].u.int64 = 2;
        values[2].format = MPV_FORMAT_STRING;
        values[2].u.string = format;
        values[3].format = MPV_FORMAT_BYTE_ARRAY;
        values[3].u.ba = &bytes;
        mpv_node_list list{4, values, keys};
        mpv_node node{};
        node.format = MPV_FORMAT_NODE_MAP;
        node.u.list = &list;
        QVERIFY(ThumbnailGenerator::imageFromScreenshotNode(node).isNull());
        values[0].u.int64 = std::numeric_limits<qint64>::max();
        QVERIFY(ThumbnailGenerator::imageFromScreenshotNode(node).isNull());
        values[0].u.int64 = 2;
        QVERIFY(ThumbnailGenerator::imageFromScreenshotNode(node).isNull()); // truncated pixels
    }

    void rejectsIncompleteScreenshotMap()
    {
        mpv_node_list list{1, nullptr, nullptr};
        mpv_node node{};
        node.format = MPV_FORMAT_NODE_MAP;
        node.u.list = &list;
        QVERIFY(ThumbnailGenerator::imageFromScreenshotNode(node).isNull());
        QVERIFY(ThumbnailGenerator::thumbnailFromScreenshotNode(node, QSize(240, 240)).isNull());
    }

    void thumbnailOwnsPixelsAndHonorsPaddedRows()
    {
        RawScreenshot frame(8, 8, 12);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const size_t offset = size_t(y * 44 + x * 4);
                frame.pixels[offset] = 255;
                frame.pixels[offset + 3] = 255;
            }
        }
        const QImage thumbnail = ThumbnailGenerator::thumbnailFromScreenshotNode(frame.node, QSize(4, 4));
        const QImage unscaled = ThumbnailGenerator::thumbnailFromScreenshotNode(frame.node, QSize(16, 16));
        QCOMPARE(thumbnail.size(), QSize(4, 4));
        QCOMPARE(unscaled.size(), QSize(8, 8));
        std::fill(frame.pixels.begin(), frame.pixels.end(), 0);
        QCOMPARE(thumbnail.pixelColor(3, 3), QColor(Qt::red));
        QCOMPARE(unscaled.pixelColor(7, 7), QColor(Qt::red));
    }

    void thumbnailBoundsPortraitDimensionsAndBytes()
    {
        for (QSize size : {QSize(3840, 2160), QSize(100, 24000), QSize(1, 65536), QSize(20000, 4)}) {
            RawScreenshot frame(size.width(), size.height());
            const QImage thumbnail = ThumbnailGenerator::thumbnailFromScreenshotNode(frame.node, QSize(240, 240));
            QVERIFY(!thumbnail.isNull());
            QVERIFY(thumbnail.width() <= 240);
            QVERIFY(thumbnail.height() <= 240);
            QVERIFY(thumbnail.sizeInBytes() <= 240 * 240 * 4);
            const QImage capped = ThumbnailGenerator::thumbnailFromScreenshotNode(frame.node, QSize(INT_MAX, INT_MAX));
            QVERIFY(capped.width() <= 512);
            QVERIFY(capped.height() <= 512);
            QVERIFY(capped.sizeInBytes() <= PosterImageCache::MaximumImageBytes);
            // Clipboard captures keep their full resolution.
            QCOMPARE(ThumbnailGenerator::imageFromScreenshotNode(frame.node).size(), size);
        }
    }

    void malformedScreenshotNeverReachesEitherImagePath()
    {
        RawScreenshot frame(2, 2);
        auto invalid = [&] {
            QVERIFY(ThumbnailGenerator::imageFromScreenshotNode(frame.node).isNull());
            QVERIFY(ThumbnailGenerator::thumbnailFromScreenshotNode(frame.node, QSize(240, 240)).isNull());
        };
        for (qint64 width : {-1LL, 0LL, qint64(INT_MAX), std::numeric_limits<qint64>::max()}) {
            frame.values[0].u.int64 = width;
            invalid();
        }
        frame.values[0].u.int64 = 2;
        for (qint64 stride : {-1LL, 7LL, qint64(INT_MAX) + 1}) {
            frame.values[2].u.int64 = stride;
            invalid();
        }
        frame.values[2].u.int64 = 8;
        frame.bytes.size = 15;
        invalid();
        frame.bytes.size = frame.pixels.size();
        frame.bytes.data = nullptr;
        invalid();
        frame.bytes.data = frame.pixels.data();
        frame.values[3].u.string = nullptr;
        invalid();
        frame.values[3].u.string = frame.format;
        frame.values[4].u.ba = nullptr;
        invalid();
    }

    void bgr0RemainsFourBytesPerPixel()
    {
        RawScreenshot frame(2, 1);
        std::memcpy(frame.format, "bgr0", 5);
        frame.pixels = {0, 0, 255, 0, 255, 0, 0, 0};
        frame.bytes.data = frame.pixels.data();
        const QImage full = ThumbnailGenerator::imageFromScreenshotNode(frame.node);
        const QImage small = ThumbnailGenerator::thumbnailFromScreenshotNode(frame.node, QSize(8, 8));
        QCOMPARE(full.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(full.pixelColor(1, 0), QColor(Qt::blue));
        QCOMPARE(full, small);
    }

    void legacyBmpThumbnailIsBoundedAndOwned()
    {
        QImage image(40, 1200, QImage::Format_RGB32);
        image.fill(Qt::green);
        QByteArray encoded;
        QBuffer buffer(&encoded);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "BMP"));
        mpv_byte_array bytes{encoded.data(), size_t(encoded.size())};
        mpv_node node{};
        node.format = MPV_FORMAT_BYTE_ARRAY;
        node.u.ba = &bytes;
        const QImage thumbnail = ThumbnailGenerator::thumbnailFromScreenshotNode(node, QSize(240, 240));
        QCOMPARE(ThumbnailGenerator::imageFromScreenshotNode(node).size(), image.size());
        encoded.fill('\0');
        QVERIFY(thumbnail.height() <= 240);
        QVERIFY(thumbnail.width() <= 240);
        QCOMPARE(thumbnail.pixelColor(0, 0), QColor(Qt::green));
        bytes.size = size_t(INT_MAX) + 1;
        QVERIFY(ThumbnailGenerator::thumbnailFromScreenshotNode(node, QSize(240, 240)).isNull());
    }

    void postersNeverFetchRemoteMedia()
    {
        ThumbnailGenerator thumbnails;
        QVERIFY(!thumbnails.requestPoster(QStringLiteral("https://example.invalid/video.mp4")));
        QVERIFY(!thumbnails.requestPoster(QStringLiteral("\\\\server\\share\\video.mp4")));
        QVERIFY(!thumbnails.requestPoster(QStringLiteral("//server/share/video.mp4")));
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        // File existence is now asynchronous; accepting local syntax promises
        // a completion/cancellation, not that the file has already been read.
        QVERIFY(thumbnails.requestPoster(QStringLiteral("missing-video.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1000);
        QVERIFY(!thumbnails.canPreview(QStringLiteral("missing-video.mp4")));
        QVERIFY(!thumbnails.requestPoster(QStringLiteral("missing-video.mp4")));
    }

    void hoverAndPosterUseTheSameLocalPolicy()
    {
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        const QStringList remote{QStringLiteral("https://example.invalid/video.mp4"),
            QStringLiteral("https:video.mp4"), QStringLiteral("file://server/share/video.mp4"),
            QStringLiteral("file:////server/share/video.mp4"), QStringLiteral("\\\\server\\share\\video.mp4"),
            QStringLiteral("//server/share/video.mp4"), QStringLiteral("edl://https://example.invalid/video"),
            QStringLiteral("data:video/mp4;base64,AAAA")};
        for (const QString &file : remote) {
            QVERIFY(!thumbnails.canPreview(file));
            QVERIFY(thumbnails.posterKey(file).isEmpty());
            QVERIFY(!thumbnails.requestPoster(file));
            const int next = previews.count() + 1;
            thumbnails.requestThumbnail(file, 0, 240);
            QTRY_COMPARE_WITH_TIMEOUT(previews.count(), next, 1000);
            QVERIFY(!previews.last().at(3).toBool());
        }
        const QString localUrl = QUrl::fromLocalFile(sampleFile()).toString();
        QVERIFY(!thumbnails.canPreview(localUrl));
        thumbnails.refreshMetadata(localUrl);
        QTRY_VERIFY_WITH_TIMEOUT(thumbnails.canPreview(localUrl), 1000);
        QCOMPARE(thumbnails.posterKey(localUrl), thumbnails.posterKey(sampleFile()));
        thumbnails.requestThumbnail(localUrl, 0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), remote.size() + 1, 6000);
        QVERIFY(previews.last().at(3).toBool());
    }

    void slowMetadataLeavesGuiRequestsAndCancellationResponsive()
    {
        QTemporaryDir directory;
        const QString other = directory.filePath("latest.mkv");
        QVERIFY(QFile::copy(sampleFile(), other));
        const auto gate = std::make_shared<MetadataGate>();
        ThumbnailGenerator thumbnails([gate](const QString &path) { return gate->lookup(path); });
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        QSignalSpy invalidated(&thumbnails, &ThumbnailGenerator::previewInvalidated);
        QElapsedTimer elapsed;
        elapsed.start();
        thumbnails.requestThumbnail(sampleFile(), 0.0, 240);
        QVERIFY(elapsed.elapsed() < 500);
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 1000);

        bool heartbeat = false;
        QTimer::singleShot(0, &thumbnails, [&] { heartbeat = true; });
        elapsed.restart();
        for (int i = 0; i < 100; ++i) {
            QVERIFY(!thumbnails.canPreview(sampleFile()));
            QVERIFY(thumbnails.posterKey(sampleFile()).isEmpty());
            QCOMPARE(thumbnails.posterRetryDelay(sampleFile()), -1);
            QVERIFY(!thumbnails.requestPoster(sampleFile()));
            thumbnails.requestThumbnail(sampleFile(), i / 100.0, 240);
        }
        thumbnails.clear();
        thumbnails.requestThumbnail(other, 3.0, 160);
        QVERIFY(elapsed.elapsed() < 500);
        QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 500);
        QCOMPARE(invalidated.count(), 1);
        QCOMPARE(gate->calls.load(), 1); // still blocked; no extra worker/thread
        QVERIFY(!gate->onGui.load());
        QVERIFY(!gate->timedOut.load());
        QCOMPARE(previews.count(), 0);

        gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QCOMPARE(previews.first().at(0).toString(), other);
        QCOMPARE(previews.first().at(1).toDouble(), 3.0);
        QVERIFY(previews.first().at(3).toBool());
        QCOMPARE(previews.first().at(2).value<QImage>().width(), 160);
        QVERIFY(!thumbnails.canPreview(sampleFile())); // obsolete metadata was also discarded
        QVERIFY(thumbnails.canPreview(other));
        QVERIFY(!gate->onGui.load());
    }

    void metadataRefreshIsBoundedAndClearRejectsOldObservations()
    {
        QTemporaryDir directory;
        const auto gate = std::make_shared<MetadataGate>();
        ThumbnailGenerator thumbnails([gate](const QString &path) { return gate->lookup(path); });
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        QSignalSpy changes(&thumbnails, &ThumbnailGenerator::metadataChanged);
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        thumbnails.refreshMetadata(sampleFile());
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 1000);
        for (int i = 0; i < 200; ++i)
            thumbnails.refreshMetadata(directory.filePath(QString::number(i) + ".mkv"));
        QCOMPARE(thumbnails.m_metadataQueue.size(), 64);
        for (int i = 136; i < 200; ++i)
            thumbnails.refreshMetadata(directory.filePath(QString::number(i) + ".mkv"));
        QCOMPARE(thumbnails.m_metadataQueue.size(), 64); // duplicate probes coalesce
        QCOMPARE(gate->calls.load(), 1);
        QVERIFY(thumbnails.requestPoster(sampleFile())); // reserve capture ahead of queued probes
        QVERIFY(thumbnails.m_trailing && thumbnails.m_trailing->poster);
        thumbnails.clear();
        QCOMPARE(cancelled.count(), 1); // a not-yet-started poster releases its pending entry too
        QVERIFY(thumbnails.m_metadataQueue.isEmpty());
        gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!thumbnails.m_metadataActive, 1000);
        QCOMPARE(changes.count(), 0);
        QVERIFY(!thumbnails.canPreview(sampleFile()));

        thumbnails.refreshMetadata(sampleFile());
        QTRY_VERIFY_WITH_TIMEOUT(thumbnails.canPreview(sampleFile()), 1000);
        QCOMPARE(changes.count(), 1);
        const int calls = gate->calls.load();
        for (int i = 0; i < 100; ++i) {
            QVERIFY(thumbnails.canPreview(sampleFile()));
            QVERIFY(!thumbnails.posterKey(sampleFile()).isEmpty());
            thumbnails.refreshMetadata(sampleFile());
        }
        QCOMPARE(gate->calls.load(), calls); // getters and minimum-age refresh do no I/O
        for (int i = 0; i < 100; ++i)
            thumbnails.refreshMetadata(directory.filePath(QString::number(i) + ".mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!thumbnails.m_metadataActive && thumbnails.m_metadataQueue.isEmpty(), 6000);
        QCOMPARE(thumbnails.m_metadata.size(), 64); // negative observations are bounded too
        QVERIFY(!gate->onGui.load());
        QVERIFY(!gate->timedOut.load());
    }

    void previewPreemptsAPosterWaitingForMetadata()
    {
        const auto gate = std::make_shared<MetadataGate>();
        ThumbnailGenerator thumbnails([gate](const QString &path) { return gate->lookup(path); });
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        QSignalSpy posters(&thumbnails, &ThumbnailGenerator::posterReady);
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        thumbnails.refreshMetadata(sampleFile());
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 1000);
        QVERIFY(thumbnails.requestPoster(sampleFile()));
        thumbnails.requestThumbnail(sampleFile(), 3.0, 240);
        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(posters.count(), 0);
        QCOMPARE(gate->calls.load(), 1);
        gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QVERIFY(previews.first().at(3).toBool());
        QCOMPARE(previews.first().at(1).toDouble(), 3.0);
        QCOMPARE(posters.count(), 0);
    }

    void previewRevisionChangeDuringFinalValidationNeverPublishesPixels()
    {
        QTemporaryDir directory;
        const QString file = directory.filePath("preview.mkv");
        QVERIFY(QFile::copy(sampleFile(), file));
        const auto gate = std::make_shared<MetadataGate>();
        gate->blockCall = 2;
        ThumbnailGenerator thumbnails([gate](const QString &path) { return gate->lookup(path); });
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        thumbnails.requestThumbnail(file, 0.0, 240);
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 6000);
        QFile changed(file);
        QVERIFY(changed.open(QIODevice::Append));
        QCOMPARE(changed.write(QByteArray(8, '\0')), qint64(8));
        changed.close();
        gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QVERIFY(!previews.first().at(3).toBool());
        QVERIFY(previews.first().at(2).value<QImage>().isNull());
        thumbnails.requestThumbnail(file, 3.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 2, 6000);
        QVERIFY(previews.last().at(3).toBool());
        QVERIFY(!gate->onGui.load());
    }

    void cachedPosterValidationCanBeCancelledWithoutPublishing()
    {
        const auto gate = std::make_shared<MetadataGate>();
        gate->blockCall = 0;
        ThumbnailGenerator thumbnails([gate](const QString &path) { return gate->lookup(path); });
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        QSignalSpy posters(&thumbnails, &ThumbnailGenerator::posterReady);
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        QVERIFY(thumbnails.requestPoster(sampleFile()));
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 1, 6000);
        QVERIFY(posters.first().at(3).toBool());
        const QString key = thumbnails.posterKey(sampleFile());
        gate->blockCall = gate->calls.load() + 2; // validate a cached candidate
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(thumbnails.requestPoster(sampleFile()));
        QVERIFY(elapsed.elapsed() < 500);
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 1000);
        elapsed.restart();
        QVERIFY(thumbnails.canPreview(sampleFile()));
        QCOMPARE(thumbnails.posterKey(sampleFile()), key);
        QCOMPARE(thumbnails.posterRetryDelay(sampleFile()), 0);
        thumbnails.clear();
        QVERIFY(elapsed.elapsed() < 500);
        gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1000);
        QCOMPARE(cancelled.first().at(1).toString(), key);
        QCOMPARE(posters.count(), 1);
        QVERIFY(thumbnails.posterCached(key));
        QVERIFY(thumbnails.requestPoster(sampleFile()));
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 2, 1000);
        QCOMPARE(posters.last().at(1).toString(), key);
        QVERIFY(posters.last().at(3).toBool());
        QVERIFY(!gate->onGui.load());
    }

    void cachedPosterReplacementDuringValidationEvictsOldPixels()
    {
        QTemporaryDir directory;
        const QString file = directory.filePath("cached.mkv");
        QVERIFY(QFile::copy(sampleFile(), file));
        const auto gate = std::make_shared<MetadataGate>();
        gate->blockCall = 0;
        ThumbnailGenerator thumbnails([gate](const QString &path) { return gate->lookup(path); });
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        QSignalSpy posters(&thumbnails, &ThumbnailGenerator::posterReady);
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        QSignalSpy evicted(&thumbnails, &ThumbnailGenerator::posterEvicted);
        QVERIFY(thumbnails.requestPoster(file));
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 1, 6000);
        QVERIFY(posters.first().at(3).toBool());
        const QString oldKey = thumbnails.posterKey(file);
        gate->blockCall = gate->calls.load() + 2;
        QVERIFY(thumbnails.requestPoster(file));
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 1000);
        QFile changed(file);
        QVERIFY(changed.open(QIODevice::Append));
        QCOMPARE(changed.write(QByteArray(8, '\0')), qint64(8));
        changed.close();
        QCOMPARE(thumbnails.posterKey(file), oldKey); // getter does not secretly stat
        gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1000);
        QCOMPARE(cancelled.first().at(1).toString(), oldKey);
        QCOMPARE(posters.count(), 1);
        QCOMPARE(evicted.count(), 1);
        QCOMPARE(evicted.first().first().toString(), oldKey);
        QVERIFY(!thumbnails.posterCached(oldKey));
        QVERIFY(thumbnails.posterKey(file) != oldKey);
        QCOMPARE(thumbnails.posterRetryDelay(file), 0);
        bool accepted = false;
        QTRY_VERIFY_WITH_TIMEOUT(accepted || (accepted = thumbnails.requestPoster(file)), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 2, 6000);
        QCOMPARE(posters.last().at(1).toString(), thumbnails.posterKey(file));
        QVERIFY(posters.last().at(3).toBool());
    }

    void rapidSupersessionOnlyPublishesTheLatestRequest()
    {
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        thumbnails.requestThumbnail(sampleFile(), 0.0, 240);
        for (int i = 0; i < 100; ++i)
            thumbnails.requestThumbnail(sampleFile(), (i % 28) / 10.0, 240);
        thumbnails.requestThumbnail(sampleFile(), 3.0, 320);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QVERIFY(previews.last().at(3).toBool());
        QCOMPARE(previews.last().at(1).toDouble(), 3.0);
        QCOMPARE(previews.last().at(2).value<QImage>().width(), 320);
        QTest::qWait(100);
        QCOMPARE(previews.count(), 1);
    }

    void clearSuppressesQueuedCompletionAndRecovers()
    {
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        thumbnails.requestThumbnail(sampleFile(), 0.0, 240);
        // Let the worker run without processing GUI completions, then clear.
        QTest::qSleep(500);
        thumbnails.clear();
        QTest::qWait(100);
        QCOMPARE(previews.count(), 0);
        thumbnails.requestThumbnail(sampleFile(), 3.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QCOMPARE(previews.last().at(1).toDouble(), 3.0);
        QVERIFY(previews.last().at(3).toBool());
    }

    void sameFileSupersessionDuringInitialLoadUsesTheLatestFrame()
    {
        for (int delay : {5, 20}) {
            ThumbnailGenerator thumbnails;
            QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
            thumbnails.requestThumbnail(sampleFile(), 0.0, 240);
            // Keep GUI completions queued while the decoder begins loading.
            QTest::qSleep(delay);
            thumbnails.requestThumbnail(sampleFile(), 3.0, 240);
            QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
            QCOMPARE(previews.last().at(1).toDouble(), 3.0);
            QVERIFY(previews.last().at(3).toBool());
            const QImage latest = previews.last().at(2).value<QImage>();
            thumbnails.requestThumbnail(sampleFile(), 3.0, 240);
            QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 2, 6000);
            QCOMPARE(latest, previews.last().at(2).value<QImage>());
        }
    }

    void clearAndFileSwitchDiscardAnInheritedInitialLoad()
    {
        QTemporaryDir directory;
        const QString other = directory.filePath("other.mkv");
        QVERIFY(QFile::copy(sampleFile(), other));
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        thumbnails.requestThumbnail(sampleFile(), 0.0, 240);
        QTest::qSleep(5);
        thumbnails.requestThumbnail(sampleFile(), 3.0, 240);
        QTest::qSleep(10);
        thumbnails.clear();
        thumbnails.requestThumbnail(other, 1.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QCOMPARE(previews.last().at(0).toString(), other);
        QCOMPARE(previews.last().at(1).toDouble(), 1.0);
        QVERIFY(previews.last().at(3).toBool());
    }

    void supersessionInterruptsTheSeekPollingBudget()
    {
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        thumbnails.requestThumbnail(sampleFile(), 0.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QVERIFY(previews.last().at(3).toBool());
        // An unreachable timestamp deterministically keeps the worker in its
        // three-second seek wait, without requiring a slow decoder fixture.
        thumbnails.requestThumbnail(sampleFile(), 100000.0, 240);
        QTest::qWait(100);
        QCOMPARE(previews.count(), 1);
        QElapsedTimer latest;
        latest.start();
        thumbnails.requestThumbnail(sampleFile(), 3.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 2, 2000);
        QVERIFY(latest.elapsed() < 2000);
        QCOMPARE(previews.last().at(1).toDouble(), 3.0);
        QVERIFY(previews.last().at(3).toBool());
    }

    void fileSwitchAndSizeChangeSupersedeOldFrames()
    {
        QTemporaryDir directory;
        const QString other = directory.filePath("other.mkv");
        QVERIFY(QFile::copy(sampleFile(), other));
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        thumbnails.requestThumbnail(sampleFile(), 0.0, 240);
        thumbnails.requestThumbnail(other, 0.0, 240);
        thumbnails.requestThumbnail(other, 0.0, 160);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QCOMPARE(previews.last().at(0).toString(), other);
        QCOMPARE(previews.last().at(2).value<QImage>().width(), 160);
        QVERIFY(previews.last().at(3).toBool());
    }

    void canonicalAliasesCompleteForTheLatestCallerPath()
    {
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        const QString url = QUrl::fromLocalFile(sampleFile()).toString();
        thumbnails.requestThumbnail(sampleFile(), 0.0, 240);
        thumbnails.requestThumbnail(url, 0.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QCOMPARE(previews.last().at(0).toString(), url);
        QVERIFY(previews.last().at(3).toBool());
    }

    void shutdownDuringCapture()
    {
        const QString file = QFileInfo(QStringLiteral("assets/test/sample.mkv")).absoluteFilePath();
        for (int delay : {0, 15, 50, 100}) {
            auto thumbnails = std::make_unique<ThumbnailGenerator>();
            QSignalSpy previews(thumbnails.get(), &ThumbnailGenerator::thumbnailReady);
            thumbnails->requestThumbnail(file, 0.0, 240);
            QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
            thumbnails->requestThumbnail(file, 3.0, 240);
            QTest::qWait(delay);
            thumbnails.reset();
        }
    }

    void shutdownWithCancelledAndTrailingRequests()
    {
        QElapsedTimer elapsed;
        elapsed.start();
        auto thumbnails = std::make_unique<ThumbnailGenerator>();
        QVERIFY(thumbnails->requestPoster(sampleFile()));
        thumbnails->requestThumbnail(sampleFile(), 0.0, 240);
        thumbnails->requestThumbnail(sampleFile(), 3.0, 240);
        thumbnails->clear();
        thumbnails->requestThumbnail(sampleFile(), 1.0, 240);
        QTest::qWait(10);
        thumbnails.reset();
        QVERIFY(elapsed.elapsed() < 5000);
    }

    void posterAndSeekResultsStaySeparate()
    {
        const QString file = QFileInfo(QStringLiteral("assets/test/sample.mkv")).absoluteFilePath();
        QVERIFY(QFileInfo::exists(file));
        ThumbnailGenerator thumbnails;
        QSignalSpy posters(&thumbnails, &ThumbnailGenerator::posterReady);
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        QVERIFY(thumbnails.requestPoster(file));
        // A second poster must yield to the current capture, while a seek
        // gets the trailing slot and cannot be lost behind a shelf of art.
        QVERIFY(!thumbnails.requestPoster(file));
        thumbnails.requestThumbnail(file, 0.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 6000);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QCOMPARE(posters.count(), 0);
        QCOMPARE(previews.first().at(0).toString(), file);
        QCOMPARE(previews.first().at(1).toDouble(), 0.0);
        QVERIFY(previews.first().at(3).toBool());

        // Cancelled work is retryable after the interactive quiet period.
        QCOMPARE(thumbnails.posterRetryDelay(file), 0);
        bool accepted = false;
        QTRY_VERIFY_WITH_TIMEOUT(accepted || (accepted = thumbnails.requestPoster(file)), 2500);
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 1, 6000);
        QVERIFY(posters.first().at(3).toBool());

        // The cached poster is revalidated asynchronously on the worker,
        // with the same stable key, without contaminating seek previews.
        QVERIFY(thumbnails.requestPoster(file));
        QCOMPARE(posters.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 2, 1000);
        QCOMPARE(posters.at(0).at(1), posters.at(1).at(1));
        QCOMPARE(previews.count(), 1);
    }

    void newPostersYieldAfterASeek()
    {
        const QString file = QFileInfo(QStringLiteral("assets/test/sample.mkv")).absoluteFilePath();
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString other = temporary.filePath(QStringLiteral("other.mkv"));
        QVERIFY(QFile::copy(file, other));
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        QSignalSpy posters(&thumbnails, &ThumbnailGenerator::posterReady);
        thumbnails.requestThumbnail(file, 0.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        // Idle gaps between pointer movements must remain available to the
        // seek preview instead of starting unrelated background decoding.
        QVERIFY(!thumbnails.requestPoster(other));
        QCOMPARE(posters.count(), 0);
        bool accepted = false;
        QTRY_VERIFY_WITH_TIMEOUT(accepted || (accepted = thumbnails.requestPoster(other)), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 1, 6000);
        QVERIFY(posters.first().at(3).toBool());
    }

    void firstPictureAndPlaybackPressureYieldWithoutDisablingPreload()
    {
        QTemporaryDir directory;
        const QString other = directory.filePath("other.mkv");
        QVERIFY(QFile::copy(sampleFile(), other));
        ThumbnailGenerator thumbnails;
        QSignalSpy posters(&thumbnails, &ThumbnailGenerator::posterReady);
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        thumbnails.setPlaybackPressure(true, true, false);
        QVERIFY(!thumbnails.backgroundWorkAllowed());
        QVERIFY(!thumbnails.requestPoster(sampleFile()));
        thumbnails.setPlaybackPressure(false, true, false);
        QVERIFY(thumbnails.requestPoster(sampleFile()));
        thumbnails.setPlaybackPressure(false, true, true);
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 6000);
        QCOMPARE(posters.count(), 0);
        thumbnails.setPlaybackPressure(false, true, false);
        QVERIFY(thumbnails.requestPoster(sampleFile()));
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 1, 6000);
        QVERIFY(posters.last().at(3).toBool());
        QVERIFY(!thumbnails.requestPoster(other));
        bool accepted = false;
        QTRY_VERIFY_WITH_TIMEOUT(accepted || (accepted = thumbnails.requestPoster(other)), 2500);
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 2, 6000);
        QVERIFY(posters.last().at(3).toBool());
    }

    void replacementChangesIdentityAndEvictsPreviousRevision()
    {
        QTemporaryDir directory;
        const QString file = directory.filePath("replacement.mkv");
        QVERIFY(QFile::copy(sampleFile(), file));
        ThumbnailGenerator thumbnails;
        QSignalSpy posters(&thumbnails, &ThumbnailGenerator::posterReady);
        QSignalSpy evicted(&thumbnails, &ThumbnailGenerator::posterEvicted);
        thumbnails.refreshMetadata(file);
        QTRY_VERIFY_WITH_TIMEOUT(thumbnails.canPreview(file), 1000);
        const QString originalKey = thumbnails.posterKey(file);
        const QString alias = directory.path() + "/./replacement.mkv";
        thumbnails.refreshMetadata(alias);
        QTRY_COMPARE_WITH_TIMEOUT(originalKey, thumbnails.posterKey(alias), 1000);
#ifdef Q_OS_WIN
        QCOMPARE(originalKey, thumbnails.posterKey(file.toUpper()));
#endif
        QVERIFY(thumbnails.requestPoster(file));
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 1, 6000);
        QVERIFY(thumbnails.posterCached(originalKey));
        QFile replacement(file);
        QVERIFY(replacement.open(QIODevice::Append));
        QCOMPARE(replacement.write(QByteArray(8, '\0')), qint64(8));
        replacement.close();
        QCOMPARE(thumbnails.posterKey(file), originalKey); // explicitly a cached observation
        thumbnails.refreshMetadata(file, true);
        QTRY_VERIFY_WITH_TIMEOUT(thumbnails.posterKey(file) != originalKey, 1000);
        const QString replacementKey = thumbnails.posterKey(file);
        QVERIFY(replacementKey != originalKey);
        bool accepted = false;
        QTRY_VERIFY_WITH_TIMEOUT(accepted || (accepted = thumbnails.requestPoster(file)), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 2, 6000);
        QCOMPARE(evicted.count(), 1);
        QCOMPARE(evicted.first().first().toString(), originalKey);
        QVERIFY(!thumbnails.posterCached(originalKey));
        QVERIFY(thumbnails.posterCached(replacementKey));
        QCOMPARE(posters.last().at(1).toString(), replacementKey);
        QVERIFY(posters.last().at(3).toBool());
        QVERIFY(thumbnails.posterKey(alias).isEmpty()); // invalidate the old alias observation
        thumbnails.refreshMetadata(alias, true);
        QTRY_COMPARE_WITH_TIMEOUT(thumbnails.posterKey(alias), replacementKey, 1000);
    }

    void failedPostersRetryWithinABudgetAndRecoverAfterReplacement()
    {
        QTemporaryDir directory;
        const QString file = directory.filePath("recover.mkv");
        QFile invalid(file);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QVERIFY(invalid.write("not a video") > 0);
        invalid.close();
        ThumbnailGenerator thumbnails;
        QSignalSpy posters(&thumbnails, &ThumbnailGenerator::posterReady);
        thumbnails.refreshMetadata(file);
        QTRY_VERIFY_WITH_TIMEOUT(thumbnails.canPreview(file), 1000);
        const QString failedKey = thumbnails.posterKey(file);
        for (int attempt = 1; attempt <= 3; ++attempt) {
            bool accepted = false;
            QTRY_VERIFY_WITH_TIMEOUT(accepted || (accepted = thumbnails.requestPoster(file)), 3000);
            QTRY_COMPARE_WITH_TIMEOUT(posters.count(), attempt, 6000);
            QVERIFY(!posters.last().at(3).toBool());
            QVERIFY(!thumbnails.posterCached(failedKey));
            QVERIFY(!thumbnails.requestPoster(file));
            if (attempt < 3)
                QVERIFY(thumbnails.posterRetryDelay(file) > 0);
        }
        QCOMPARE(thumbnails.posterRetryDelay(file), -1);
        QTest::qWait(100);
        QVERIFY(!thumbnails.requestPoster(file));
        const QString alias = directory.path() + "/./recover.mkv";
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        bool aliasAccepted = false;
        QTRY_VERIFY_WITH_TIMEOUT(aliasAccepted || (aliasAccepted = thumbnails.requestPoster(alias)), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1000);
        QCOMPARE(posters.count(), 3); // an unknown alias cannot spend a fourth decode attempt
        QCOMPARE(thumbnails.posterKey(alias), failedKey);
        QCOMPARE(thumbnails.posterRetryDelay(alias), -1);
        QVERIFY(QFile::remove(file));
        QVERIFY(QFile::copy(sampleFile(), file));
        thumbnails.refreshMetadata(file, true);
        QTRY_VERIFY_WITH_TIMEOUT(thumbnails.posterKey(file) != failedKey, 1000);
        QCOMPARE(thumbnails.posterRetryDelay(file), 0);
        bool accepted = false;
        QTRY_VERIFY_WITH_TIMEOUT(accepted || (accepted = thumbnails.requestPoster(file)), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 4, 6000);
        QVERIFY(posters.last().at(3).toBool());
    }

    void revisionChangeDuringCaptureDiscardsTheOldResult()
    {
        QTemporaryDir directory;
        const QString file = directory.filePath("inflight.mkv");
        QVERIFY(QFile::copy(sampleFile(), file));
        const auto gate = std::make_shared<MetadataGate>();
        gate->blockCall = 2; // final validation, after a real capture candidate
        ThumbnailGenerator thumbnails([gate](const QString &path) { return gate->lookup(path); });
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        QSignalSpy posters(&thumbnails, &ThumbnailGenerator::posterReady);
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        QVERIFY(thumbnails.requestPoster(file));
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 6000);
        QFile changed(file);
        QVERIFY(changed.open(QIODevice::Append));
        QCOMPARE(changed.write(QByteArray(8, '\0')), qint64(8));
        changed.close();
        gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 6000);
        QCOMPARE(posters.count(), 0);
        QCOMPARE(thumbnails.posterCache()->count(), 0);
        QCOMPARE(thumbnails.posterRetryDelay(file), 0);
        QVERIFY(thumbnails.requestPoster(file));
        QTRY_COMPARE_WITH_TIMEOUT(posters.count(), 1, 6000);
        QCOMPARE(posters.first().at(1).toString(), thumbnails.posterKey(file));
        QVERIFY(posters.first().at(3).toBool());
    }

    void previewRecoversAfterIdleRelease()
    {
        const QString file = QFileInfo(QStringLiteral("assets/test/sample.mkv")).absoluteFilePath();
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        thumbnails.requestThumbnail(file, 0.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QVERIFY(previews.first().at(3).toBool());

        // The idle worker releases its decoder after five seconds. Reusing
        // the same path must reload it, rather than seek an empty new core.
        QTest::qWait(5500);
        thumbnails.requestThumbnail(file, 0.5, 240);
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 2, 6000);
        QVERIFY(previews.at(1).at(3).toBool());
        QCOMPARE(previews.at(1).at(1).toDouble(), 0.5);
        QCOMPARE(previews.at(1).at(2).value<QImage>().width(), 240);
    }

    void sequentialPreviewsUseTheRequestedFrame()
    {
        const QString file = QFileInfo(QStringLiteral("assets/test/sample.mkv")).absoluteFilePath();
        ThumbnailGenerator thumbnails;
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        for (double seconds : {0.0, 3.0, 0.0}) {
            const int next = previews.count() + 1;
            thumbnails.requestThumbnail(file, seconds, 240);
            QTRY_COMPARE_WITH_TIMEOUT(previews.count(), next, 6000);
            QVERIFY(previews.last().at(3).toBool());
        }
        const QImage beginning = previews.at(0).at(2).value<QImage>();
        const QImage later = previews.at(1).at(2).value<QImage>();
        const QImage returned = previews.at(2).at(2).value<QImage>();
        // The bundled test pattern moves. A successful but stale capture
        // would return the beginning again after seeking three seconds.
        QVERIFY(beginning != later);
        QCOMPARE(returned, beginning);
    }
};

QTEST_GUILESS_MAIN(TestThumbnails)
#include "test_thumbnails.moc"
