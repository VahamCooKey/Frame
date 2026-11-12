// Audio-file overview for the audio view: title/artist/album tags, the cover
// (embedded art or mpv's folder art such as cover.jpg) and a whole-track
// waveform. Uses its own private mpv handles on a worker thread, never the
// player's handle. One request at a time; a newer load() cancels the older.
#pragma once
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <thread>

class AudioCoverStore {
public:
    void set(const QImage &image) { QMutexLocker lock(&m_mutex); m_image = image; }
    QImage get() const { QMutexLocker lock(&m_mutex); return m_image; }
private:
    mutable QMutex m_mutex;
    QImage m_image;
};

// image://audiocover/<n> always serves the latest cover; n only busts caches.
class AudioCoverProvider final : public QQuickImageProvider {
public:
    explicit AudioCoverProvider(std::shared_ptr<AudioCoverStore> store)
        : QQuickImageProvider(QQuickImageProvider::Image), m_store(std::move(store)) {}
    QImage requestImage(const QString &, QSize *size, const QSize &requested) override
    {
        QImage image = m_store->get();
        if (!image.isNull() && requested.isValid() && !requested.isEmpty()
            && (image.width() > requested.width() || image.height() > requested.height()))
            image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (size) *size = image.size();
        return image;
    }
private:
    std::shared_ptr<AudioCoverStore> m_store;
};

class AudioOverview : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString coverSource READ coverSource NOTIFY changed)
    Q_PROPERTY(QVariantList peaks READ peaks NOTIFY peaksChanged)
public:
    // Peaks are normalised to 0..1, one per bucket.
    static constexpr int PeakCount = 600;

    explicit AudioOverview(QObject *parent = nullptr);
    ~AudioOverview() override;

    // Local files only; anything else clears. Same path again is a no-op.
    Q_INVOKABLE void load(const QString &path);
    Q_INVOKABLE void clear();

    QString path() const { return m_path; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString coverSource() const { return m_coverSource; }
    QVariantList peaks() const { return m_peaks; }
    std::shared_ptr<AudioCoverStore> coverStore() const { return m_cover; }

    // Exposed for tests: peak buckets from mono s16 samples.
    static QVariantList peaksFromSamples(const qint16 *samples, qsizetype count, int buckets);

signals:
    void changed();
    void peaksChanged();

private:
    struct Tags { QString title, artist, album; double duration{0}; };
    void cancelWorker();
    void run(quint64 generation, QString path);
    bool cancelled(quint64 generation) const { return m_generation.load() != generation; }
    Tags readTagsAndCover(quint64 generation, const QString &path, QImage &cover);
    QVariantList decodePeaks(quint64 generation, const QString &path, double duration);

    std::shared_ptr<AudioCoverStore> m_cover = std::make_shared<AudioCoverStore>();
    std::atomic<quint64> m_generation{0};
    std::thread m_worker;
    int m_coverSerial{0};
    QString m_path, m_title, m_artist, m_album, m_coverSource;
    QVariantList m_peaks;
};
