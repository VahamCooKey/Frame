#include "core/ThumbnailGenerator.h"
#include "core/ThumbnailPosterProvider.h"

#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QJSValue>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <atomic>
#include <thread>

class PosterTestPlayer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList playlist MEMBER playlist NOTIFY playlistChanged)
    Q_PROPERTY(int playlistPos MEMBER playlistPos NOTIFY playlistPosChanged)
    Q_PROPERTY(int state MEMBER state NOTIFY stateChanged)
    Q_PROPERTY(bool scrubbing MEMBER scrubbing NOTIFY scrubbingChanged)
    Q_PROPERTY(int videoWidth MEMBER videoWidth NOTIFY videoSizeChanged)
    Q_PROPERTY(bool videoPresented MEMBER videoPresented NOTIFY presentationStateChanged)
public:
    QVariantList playlist;
    int playlistPos{0};
    int state{3};
    bool scrubbing{false};
    int videoWidth{1920};
    bool videoPresented{false};
    void present() { videoPresented = true; emit presentationStateChanged(); }
signals:
    void playlistChanged();
    void playlistPosChanged();
    void stateChanged();
    void scrubbingChanged();
    void videoSizeChanged();
    void presentationStateChanged();
    void fileLoaded(const QString &path);
};

class TestPosterCache : public QObject {
    Q_OBJECT
private:
    static QString sampleFile()
    {
        return QFileInfo(QStringLiteral("assets/test/sample.mkv")).absoluteFilePath();
    }

    static std::unique_ptr<QObject> createQml(QQmlEngine &engine, PosterTestPlayer &player,
                                             ThumbnailGenerator &thumbnails, const QString &file)
    {
        player.playlist = {QVariantMap{{"filename", file}}};
        // Match main.cpp's ownership of pressure/presentation wiring.
        const auto pressure = [&player, &thumbnails] {
            thumbnails.setPlaybackPressure(player.videoWidth > 0 && !player.videoPresented,
                player.state == 3, player.scrubbing || (player.state != 3 && player.state != 4));
        };
        QObject::connect(&player, &PosterTestPlayer::stateChanged, &thumbnails, pressure);
        QObject::connect(&player, &PosterTestPlayer::scrubbingChanged, &thumbnails, pressure);
        QObject::connect(&player, &PosterTestPlayer::videoSizeChanged, &thumbnails, pressure);
        QObject::connect(&player, &PosterTestPlayer::presentationStateChanged, &thumbnails, pressure);
        pressure();
        engine.rootContext()->setContextProperty("Player", &player);
        engine.rootContext()->setContextProperty("Thumbs", &thumbnails);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(FRAME_QML_DIR "/PlaylistPosterCache.qml")));
        auto result = std::unique_ptr<QObject>(component.create());
        if (!result)
            qWarning().noquote() << component.errorString();
        return result;
    }

    static QString sourceFor(QObject *root, const QString &file)
    {
        QVariant result;
        const bool invoked = QMetaObject::invokeMethod(root, "sourceFor", Q_RETURN_ARG(QVariant, result),
                                                       Q_ARG(QVariant, file));
        return invoked ? result.toString() : QStringLiteral("invoke-error");
    }

    static QVariantMap entries(QObject *root)
    {
        const QVariant value = root->property("entries");
        return value.metaType() == QMetaType::fromType<QJSValue>()
            ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
    }

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(".*"));
    }

    void countBudgetAndAccessOrder()
    {
        PosterImageCache cache;
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(Qt::white);
        for (int i = 0; i < 32; ++i)
            QVERIFY(cache.insert(QString::number(i), QString::number(i), image).isEmpty());
        QVERIFY(!cache.image("0").isNull()); // keep the oldest entry recently used
        const QStringList evicted = cache.insert("32", "32", image);
        QCOMPARE(evicted, QStringList{"1"});
        QVERIFY(cache.contains("0"));
        QVERIFY(!cache.contains("1"));
        for (int i = 33; i < 80; ++i)
            cache.insert(QString::number(i), QString::number(i), image);
        QCOMPARE(cache.count(), 32);
        QCOMPARE(cache.byteSize(), 32 * image.sizeInBytes());
    }

    void byteAndDimensionBudgets()
    {
        PosterImageCache cache;
        QImage image(512, 512, QImage::Format_RGB32);
        image.fill(Qt::black);
        for (int i = 0; i < 40; ++i) {
            cache.insert(QString::number(i), QString::number(i), image.copy());
            QVERIFY(cache.byteSize() <= PosterImageCache::MaximumBytes);
            QVERIFY(cache.count() <= PosterImageCache::MaximumEntries);
        }
        QCOMPARE(cache.count(), 8);
        QCOMPARE(cache.byteSize(), PosterImageCache::MaximumBytes);
        QImage tall(1, 65536, QImage::Format_RGB32);
        cache.insert("tall", "tall", tall);
        QVERIFY(!cache.contains("tall"));
        cache.insert("empty", "empty", {});
        QVERIFY(!cache.contains("empty"));
    }

    void providerSharesRevisionEvictionAndPixels()
    {
        auto cache = std::make_shared<PosterImageCache>();
        ThumbnailPosterProvider provider(cache);
        QImage first(40, 30, QImage::Format_RGB32);
        first.fill(Qt::red);
        cache->insert("revision-1", "canonical-file", first);
        QSize size;
        QCOMPARE(provider.requestImage("revision-1", &size, {}).cacheKey(), first.cacheKey());
        QCOMPARE(size, first.size());
        QCOMPARE(cache->removeOtherRevisions("canonical-file", "revision-2"), QStringList{"revision-1"});
        QVERIFY(provider.requestImage("revision-1", &size, {}).isNull());
        QVERIFY(size.isEmpty());
        QImage second(40, 30, QImage::Format_RGB32);
        second.fill(Qt::blue);
        cache->insert("revision-2", "canonical-file", second);
        QCOMPARE(provider.requestImage("revision-2", nullptr, {}), second);
    }

    void providerAndGeneratorCanBeDestroyedInEitherOrder()
    {
        std::unique_ptr<ThumbnailPosterProvider> provider;
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(Qt::green);
        {
            ThumbnailGenerator thumbnails;
            thumbnails.posterCache()->insert("held", "file", image);
            provider = std::make_unique<ThumbnailPosterProvider>(thumbnails.posterCache());
        }
        QCOMPARE(provider->requestImage("held", nullptr, {}), image);
        provider.reset();
    }

    void providerReadsDuringEviction()
    {
        auto cache = std::make_shared<PosterImageCache>(4096, 4);
        ThumbnailPosterProvider provider(cache);
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(Qt::white);
        std::atomic<bool> valid{true};
        std::thread reader([&] {
            for (int i = 0; i < 2000; ++i) {
                const QImage read = provider.requestImage(QString::number(i % 8), nullptr, {});
                if (!read.isNull() && (read.size() != image.size() || read.pixelColor(0, 0) != QColor(Qt::white)))
                    valid = false;
            }
        });
        for (int i = 0; i < 2000; ++i)
            cache->insert(QString::number(i % 8), QString::number(i % 8), image);
        reader.join();
        QVERIFY(valid);
        QVERIFY(cache->byteSize() <= 4096);
        QVERIFY(cache->count() <= 4);
    }

    void qmlWaitsForFirstPictureAndContinuesDuringPlayback()
    {
        PosterTestPlayer player;
        ThumbnailGenerator thumbnails;
        QQmlEngine engine;
        auto root = createQml(engine, player, thumbnails, sampleFile());
        QVERIFY(root);
        root->setProperty("warmed", true);
        QSignalSpy ready(&thumbnails, &ThumbnailGenerator::posterReady);
        QVERIFY(!root->property("firstPictureReady").toBool());
        QVERIFY(!thumbnails.backgroundWorkAllowed());
        QTest::qWait(450);
        QCOMPARE(ready.count(), 0);
        player.present();
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 6000);
        QVERIFY(ready.first().at(3).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!sourceFor(root.get(), sampleFile()).isEmpty(), 1000);
        QCOMPARE(sourceFor(root.get(), sampleFile()), "image://poster/" + ready.first().at(1).toString());
        player.state = 2; // buffering
        emit player.stateChanged();
        QVERIFY(!thumbnails.backgroundWorkAllowed());
    }

    void qmlAudioOnlyDoesNotWaitForAVideoSwap()
    {
        PosterTestPlayer player;
        player.videoWidth = 0;
        ThumbnailGenerator thumbnails;
        QQmlEngine engine;
        auto root = createQml(engine, player, thumbnails, sampleFile());
        QVERIFY(root);
        QVERIFY(root->property("firstPictureReady").toBool());
        QVERIFY(thumbnails.backgroundWorkAllowed());
    }

    void qmlCancelledPendingPosterCanResume()
    {
        PosterTestPlayer player;
        player.videoPresented = true;
        ThumbnailGenerator thumbnails;
        QQmlEngine engine;
        auto root = createQml(engine, player, thumbnails, sampleFile());
        QVERIFY(root);
        QSignalSpy cancelled(&thumbnails, &ThumbnailGenerator::posterCancelled);
        QSignalSpy previews(&thumbnails, &ThumbnailGenerator::thumbnailReady);
        // Cold eligibility is now a worker observation, not a synchronous
        // filesystem query inside prepareNext(). Exercise the real QML probe.
        QVERIFY(QMetaObject::invokeMethod(root.get(), "prepareNext"));
        QTRY_VERIFY_WITH_TIMEOUT(thumbnails.canPreview(sampleFile()), 1000);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "prepareNext"));
        QVERIFY(entries(root.get()).value(sampleFile()).toMap().value("pending").toBool());
        thumbnails.requestThumbnail(sampleFile(), 3.0, 240);
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 6000);
        QVERIFY(!entries(root.get()).contains(sampleFile()));
        QTRY_COMPARE_WITH_TIMEOUT(previews.count(), 1, 6000);
        QTRY_VERIFY_WITH_TIMEOUT(!sourceFor(root.get(), sampleFile()).isEmpty(), 6000);
    }

    void qmlEvictionClearsTheProviderAndSource()
    {
        PosterTestPlayer player;
        player.videoPresented = true;
        ThumbnailGenerator thumbnails;
        ThumbnailPosterProvider provider(thumbnails.posterCache());
        QQmlEngine engine;
        auto root = createQml(engine, player, thumbnails, sampleFile());
        QVERIFY(root);
        root->setProperty("warmed", true);
        QTRY_VERIFY_WITH_TIMEOUT(!sourceFor(root.get(), sampleFile()).isEmpty(), 6000);
        const QString oldKey = thumbnails.posterKey(sampleFile());
        QVERIFY(!provider.requestImage(oldKey, nullptr, {}).isNull());
        QImage other(400, 400, QImage::Format_RGB32);
        other.fill(Qt::black);
        for (int i = 0; i < 40; ++i) {
            const QStringList evicted = thumbnails.posterCache()->insert(QString::number(i), QString::number(i), other);
            for (const QString &key : evicted)
                emit thumbnails.posterEvicted(key);
        }
        QVERIFY(provider.requestImage(oldKey, nullptr, {}).isNull());
        QCOMPARE(sourceFor(root.get(), sampleFile()), QString());
    }

    void qmlDetectsAReplacementAfterPreloadFinished()
    {
        QTemporaryDir directory;
        const QString file = directory.filePath("replace.mkv");
        QVERIFY(QFile::copy(sampleFile(), file));
        PosterTestPlayer player;
        player.videoPresented = true;
        ThumbnailGenerator thumbnails;
        QQmlEngine engine;
        auto root = createQml(engine, player, thumbnails, file);
        QVERIFY(root);
        root->setProperty("warmed", true);
        QTRY_VERIFY_WITH_TIMEOUT(!sourceFor(root.get(), file).isEmpty(), 6000);
        const QString oldSource = sourceFor(root.get(), file);
        QFile changed(file);
        QVERIFY(changed.open(QIODevice::Append));
        QVERIFY(changed.write(QByteArray(8, '\0')) == 8);
        changed.close();
        // Getters deliberately retain the last observation. The QML periodic
        // refresh must discover the new revision and replace its completed art.
        QCOMPARE("image://poster/" + thumbnails.posterKey(file), oldSource);
        QTRY_VERIFY_WITH_TIMEOUT(!sourceFor(root.get(), file).isEmpty()
                                 && sourceFor(root.get(), file) != oldSource, 6500);
        QCOMPARE(sourceFor(root.get(), file), "image://poster/" + thumbnails.posterKey(file));
    }

    void qmlRetriesFailuresAndRecoversFromANewRevision()
    {
        QTemporaryDir directory;
        const QString file = directory.filePath("recover.mkv");
        QFile invalid(file);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QVERIFY(invalid.write("not a video") > 0);
        invalid.close();
        PosterTestPlayer player;
        player.videoPresented = true;
        ThumbnailGenerator thumbnails;
        QQmlEngine engine;
        auto root = createQml(engine, player, thumbnails, file);
        QVERIFY(root);
        root->setProperty("warmed", true);
        QSignalSpy ready(&thumbnails, &ThumbnailGenerator::posterReady);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 8000);
        for (const auto &result : ready)
            QVERIFY(!result.at(3).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(entries(root.get()).value(file).toMap().value("failed").toBool(), 1000);
        QCOMPARE(thumbnails.posterRetryDelay(file), -1);
        QTest::qWait(2300); // periodic revalidation cannot reset an exhausted budget
        QCOMPARE(ready.count(), 3);
        QVERIFY(QFile::remove(file));
        QVERIFY(QFile::copy(sampleFile(), file));
        QTRY_VERIFY_WITH_TIMEOUT(!sourceFor(root.get(), file).isEmpty(), 6500);
        QCOMPARE(ready.count(), 4);
        QVERIFY(ready.last().at(3).toBool());
    }
};

QTEST_MAIN(TestPosterCache)
#include "test_postercache.moc"
