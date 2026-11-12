// CONTRACT v3 — owned by lead. Amend via CONTRACTS.md only.
// v3: A58 — worker metadata validation and cached GUI eligibility/revisions.
// v2: amendment A38 — low-priority requestPoster(), stable posterReady IDs,
//     and a bounded 32-image cache sharing the existing decoder worker.
// v1: amendment A21 — imageFromScreenshotNode(), the screenshot-raw RESULT
//     decoder, now shared with PlayerController's F9 copy-frame path. The
//     class predates the banner numbering; this is its first banner.
//
// On-demand frame thumbnails for the seek preview. Uses its own private mpv
// handle (never the app handle owned by PlayerController/MpvRenderItem) that
// lives and dies on a dedicated worker thread. Extracts a frame at a given
// time via loadfile + seek + screenshot-raw, then emits it as a QImage on
// the GUI thread.
#pragma once
#include <QImage>
#include <QElapsedTimer>
#include <QCache>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>
#include <memory>
#include <optional>

#include "core/PosterImageCache.h"
#include "core/ThumbnailMetadata_p.h"

class QThread;

// Forward-declared rather than including <mpv/client.h>: everything that
// only wants a thumbnail would otherwise pull mpv into its translation unit.
struct mpv_node;

class ThumbnailGenerator : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool backgroundWorkAllowed READ backgroundWorkAllowed NOTIFY posterSchedulingChanged)
public:
    explicit ThumbnailGenerator(QObject *parent = nullptr);
    ~ThumbnailGenerator() override;

    // Local media only. Both dimensions are capped by maxWidth and by 512;
    // nonpositive maxWidth uses 512. A newer request cancels obsolete work;
    // there is at most one active capture and one latest trailing request.
    Q_INVOKABLE void requestThumbnail(const QString &filePath, double seconds, int maxWidth);

    // Low-priority local playlist art. Never queues ahead of a seek preview;
    // nearby playlist items prepare in the background after playback starts.
    // A recent seek suppresses new poster work; images share a bounded cache.
    // true means asynchronous work was accepted, not that the file exists.
    // Missing/replaced/cancelled files emit posterCancelled with the key
    // known at acceptance (possibly empty); success uses a validated key.
    Q_INVOKABLE bool requestPoster(const QString &filePath);

    // Cancels active/trailing work and queued cache deliveries, preserving
    // reusable poster images. No obsolete capture publishes after this call.
    Q_INVOKABLE void clear();

    // Cached observations only: false/empty until worker validation completes.
    // These getters never stat/canonicalize and do NOT establish freshness.
    // refreshMetadata() coalesces a bounded, low-priority queue on the same
    // worker (one-second minimum observation age unless force=true).
    // metadataChanged wakes callers when an observation changes.
    // Requests also validate on the worker, including cached poster delivery.
    // Local paths/file: URLs only; URLs/UNC are rejected syntactically on GUI,
    // non-files/canonical UNC targets on worker. Mounted volumes can look local.
    Q_INVOKABLE void refreshMetadata(const QString &filePath, bool force = false);
    Q_INVOKABLE bool canPreview(const QString &filePath) const;
    Q_INVOKABLE QString posterKey(const QString &filePath) const;
    // 0 = ready to try, positive = backoff in ms, -1 = retry budget exhausted
    // or unavailable. Three attempts per file revision; failure metadata is bounded.
    Q_INVOKABLE int posterRetryDelay(const QString &filePath) const;
    Q_INVOKABLE bool posterCached(const QString &key) const;

    // First picture/buffering/seeking/scrubbing suspend background work.
    // Stable active playback still preloads, with a 1.2 s gap between posters.
    Q_INVOKABLE void setPlaybackPressure(bool firstPicturePending, bool playbackActive, bool playbackBusy);
    bool backgroundWorkAllowed() const { return !m_backgroundSuspended; }
    std::shared_ptr<PosterImageCache> posterCache() const { return m_posters; }

    // Decodes a screenshot-raw RESULT node (NODE_MAP of w/h/stride/format/
    // data). Null means the node was not a well-formed raw frame. The pixels
    // are copied, so the caller may free the node straight after — and must,
    // since this never frees it. Shared with F9's copy-frame path: the bgr0
    // trap (four bytes per pixel, not three) is exactly the kind of thing
    // that gets fixed in one copy of a decoder and not the other.
    static QImage imageFromScreenshotNode(const mpv_node &node);

    // Scales the validated borrowed screenshot while its mpv buffer is alive.
    // Always owns the returned pixels, even when no resize was necessary.
    // The caller still frees the node. Bounds are clamped to 512 x 512/1 MiB.
    static QImage thumbnailFromScreenshotNode(const mpv_node &node, QSize bounds);

signals:
    void thumbnailReady(const QString &filePath, double seconds, const QImage &image, bool success);
    void posterReady(const QString &filePath, const QString &key, const QImage &image, bool success);
    void posterCancelled(const QString &filePath, const QString &key);
    void posterEvicted(const QString &key);
    void previewInvalidated();
    void posterSchedulingChanged();
    void metadataChanged();

private:
    friend class TestThumbnails;
    explicit ThumbnailGenerator(ThumbnailMetadataLookup lookup, QObject *parent = nullptr);
    struct CaptureRequest {
        QString filePath;
        QString localPath;
        // The key known when accepted, used to release that caller's pending
        // entry on cancellation. Successful delivery uses the validated key.
        QString key;
        double seconds{0.0};
        int maxWidth{0};
        quint64 generation{0};
        bool poster{false};
        bool cacheOnly{false};
        bool retryBlocked{false};
    };
    struct MetadataRequest { QString localPath; quint64 generation{0}; };
    struct MetadataObservation { ThumbnailMetadata media; QString posterKey; qint64 checkedAt{0}; };
    struct Failure { int attempts{0}; qint64 retryAt{0}; };
    void dispatchCapture(const CaptureRequest &request);
    void startNextWork();
    void rememberMetadata(const QString &localPath, const ThumbnailMetadata &metadata);
    void onCaptureReady(quint64 generation, const ThumbnailMetadata &metadata, const QImage &image,
                        bool cancelled, bool retryBlocked);
    void onCaptureValidated(quint64 generation, const ThumbnailMetadata &metadata, const QImage &image,
                            bool cancelled, bool replaced);
    void advanceGeneration(quint64 reusableGeneration = 0);
    void evictOtherRevisions(const QString &identity, const QString &key);

    QThread *m_thread{nullptr};
    QObject *m_worker{nullptr};
    bool m_captureActive{false};
    std::shared_ptr<PosterImageCache> m_posters{std::make_shared<PosterImageCache>()};
    QCache<QString, Failure> m_failures{64};
    QCache<QString, MetadataObservation> m_metadata{64};
    QList<MetadataRequest> m_metadataQueue;
    std::optional<MetadataRequest> m_metadataActive;
    QElapsedTimer m_clock;
    QElapsedTimer m_lastPreview;
    QElapsedTimer m_lastPoster;
    quint64 m_generation{0};
    CaptureRequest m_active;
    std::optional<CaptureRequest> m_trailing;
    bool m_backgroundSuspended{false};
    bool m_playbackActive{false};
};
