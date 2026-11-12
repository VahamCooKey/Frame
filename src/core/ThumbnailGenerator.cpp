#include "core/ThumbnailGenerator.h"

#include <QDir>
#include <QCryptographicHash>
#include <QBuffer>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>
#include <QVariant>

#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

#include <mpv/client.h>

namespace {

constexpr int kLoadTimeoutMs = 1200;  // local files can still need a decoder warm-up
constexpr int kSeekTimeoutMs = 3000;  // bounded software fallback for long-GOP previews
constexpr int kPollIntervalMs = 5;    // bounded event wait keeps shutdown cancellable
constexpr int kIdleReleaseMs = 5000; // keep scrubbing warm, release idle decoder buffers
constexpr int kMaxThumbnailDimension = 512;
constexpr int kPosterDimension = 400;
constexpr int kPreviewQuietMs = 1000;
constexpr int kPosterAttempts = 3;

bool isUncPath(const QString &path)
{
    return path.startsWith(QStringLiteral("//")) || path.startsWith(QStringLiteral("\\\\"));
}

// This identifies local path syntax, not the physical storage behind a mounted
// volume. Do not query file metadata for URLs/UNC paths: that can itself reach
// the network. file: URLs without an authority are accepted consistently by
// hover and poster requests, and converted to a path before calling mpv.
std::optional<QString> localPathSyntax(const QString &filePath)
{
    if (filePath.isEmpty() || filePath.contains(QChar(u'\0')) || isUncPath(filePath))
        return std::nullopt;
    QString path = filePath;
    const QUrl url(filePath);
#ifdef Q_OS_WIN
    static const QRegularExpression drivePath(QStringLiteral("^[A-Za-z]:[/\\\\]"));
    const bool drive = drivePath.match(filePath).hasMatch();
#else
    const bool drive = false;
#endif
    if (!drive && !url.scheme().isEmpty()) {
        if (url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0
            || !url.isValid() || !url.authority().isEmpty() || url.hasQuery() || url.hasFragment())
            return std::nullopt;
        path = url.toLocalFile();
    }
    path = QDir::fromNativeSeparators(path);
    if (path.isEmpty() || path.contains(QChar(u'\0')) || isUncPath(path))
        return std::nullopt;
    return path;
}

QString metadataCacheKey(const QString &path)
{
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

} // namespace

ThumbnailMetadata lookupThumbnailMetadata(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return {};
    const QString canonical = QDir::fromNativeSeparators(info.canonicalFilePath());
    if (canonical.isEmpty() || isUncPath(canonical))
        return {};
#ifdef Q_OS_WIN
    const QString identity = canonical.toCaseFolded();
#else
    const QString identity = canonical;
#endif
    const QByteArray metadata = QByteArray::number(info.size()) + '/'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '/'
        + QByteArray::number(info.birthTime().toMSecsSinceEpoch()) + '/'
        + QByteArray::number(info.metadataChangeTime().toMSecsSinceEpoch());
    QByteArray revision = identity.toUtf8();
    revision.append('\0');
    revision.append(metadata);
    return ThumbnailMetadata{canonical, identity, QString::fromLatin1(
        QCryptographicHash::hash(revision, QCryptographicHash::Sha256).toHex())};
}

namespace {

QString posterKeyFor(const ThumbnailMetadata &media)
{
    if (!media.valid())
        return {};
    // Include output/capture policy so changing that policy cannot reuse old art.
    return QString::fromLatin1(QCryptographicHash::hash(
        media.revision.toLatin1() + "/poster-v2:400x400:8pct-30s:keyframes:video",
        QCryptographicHash::Sha256).toHex());
}

QSize boundedSize(QSize bounds)
{
    return {bounds.width() <= 0 ? kMaxThumbnailDimension : qMin(bounds.width(), kMaxThumbnailDimension),
            bounds.height() <= 0 ? kMaxThumbnailDimension : qMin(bounds.height(), kMaxThumbnailDimension)};
}

struct CaptureCancellation {
    const std::atomic<bool> &shutdown;
    const std::atomic<quint64> &latest;
    quint64 generation;
    bool cancelled() const { return shutdown.load() || latest.load() != generation; }
};

// Drains every queued event so leftover events from a previous request can
// never satisfy the next request's FILE_LOADED wait.
void drainEvents(mpv_handle *mpv, const CaptureCancellation &cancel)
{
    while (!cancel.cancelled() && mpv_wait_event(mpv, 0)->event_id != MPV_EVENT_NONE) {
    }
}

// Pumps events until FILE_LOADED, a load error, or timeout, whichever first.
bool pumpUntilFileLoaded(mpv_handle *mpv, int timeoutMs, const CaptureCancellation &cancel,
                         bool &loadStarted)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (cancel.cancelled())
            return false;
        mpv_event *event = mpv_wait_event(mpv, kPollIntervalMs / 1000.0);
        switch (event->event_id) {
        case MPV_EVENT_START_FILE:
            loadStarted = true;
            break;
        case MPV_EVENT_FILE_LOADED:
            if (loadStarted)
                return true;
            break;
        case MPV_EVENT_END_FILE: {
            auto *endFile = static_cast<mpv_event_end_file *>(event->data);
            if (endFile && endFile->reason == MPV_END_FILE_REASON_ERROR)
                return false;
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            return false;
        default:
            break;
        }
    }
    return false;
}

// FILE_LOADED only guarantees demuxer setup. Wait until mpv has actually
// landed on the requested timestamp before asking screenshot-raw for pixels;
// otherwise a seek preview repeatedly captures the old/first frame.
bool pumpUntilSeekLanded(mpv_handle *mpv, double target, int timeoutMs,
                         const CaptureCancellation &cancel, bool exact = true)
{
    QElapsedTimer timer;
    timer.start();
    bool seekStarted = false;
    bool playbackRestarted = false;
    while (timer.elapsed() < timeoutMs) {
        if (cancel.cancelled())
            return false;
        mpv_event *event = mpv_wait_event(mpv, kPollIntervalMs / 1000.0);
        if (event->event_id == MPV_EVENT_SEEK)
            seekStarted = true;
        else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART && seekStarted)
            playbackRestarted = true;
        else if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *endFile = static_cast<const mpv_event_end_file *>(event->data);
            if (endFile && endFile->reason == MPV_END_FILE_REASON_ERROR)
                return false;
        } else if (event->event_id == MPV_EVENT_SHUTDOWN) {
            return false;
        }

        double position = 0.0;
        if (playbackRestarted
            && mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &position) >= 0
            && (!exact || std::abs(position - target) < 0.25))
            return true;
    }
    return false;
}

} // namespace

// The screenshot-raw RESULT node is a MPV_FORMAT_NODE_MAP (keys: w, h,
// stride, format e.g. "bgr24", data=BYTE_ARRAY of raw pixels — NOT a BMP
// file) on mpv builds that use the raw-node variant. Static and shared
// because F9's copy-frame path runs the same command on the app handle.
//
// It never frees the node: the caller ran mpv_command_node and owns the
// lifetime, and every exit here leaves it intact for exactly one free.
static QImage decodeScreenshot(const mpv_node &node, QSize bounds = {})
{
    QImage image;
    if (node.format == MPV_FORMAT_NODE_MAP && node.u.list) {
        QImage::Format fmt = QImage::Format_Invalid;
        const mpv_node_list &list = *node.u.list;
        if (list.num <= 0 || !list.keys || !list.values)
            return {};
        qint64 width = 0, height = 0, stride = 0;
        const void *pixels = nullptr;
        size_t pixelSize = 0;
        for (int i = 0; i < node.u.list->num; ++i) {
            const char *key = node.u.list->keys[i];
            const mpv_node &v = node.u.list->values[i];
            if (!key)
                continue;
            if (qstrcmp(key, "w") == 0 && v.format == MPV_FORMAT_INT64)
                width = v.u.int64;
            else if (qstrcmp(key, "h") == 0 && v.format == MPV_FORMAT_INT64)
                height = v.u.int64;
            else if (qstrcmp(key, "stride") == 0 && v.format == MPV_FORMAT_INT64)
                stride = v.u.int64;
            else if (qstrcmp(key, "data") == 0 && v.format == MPV_FORMAT_BYTE_ARRAY && v.u.ba) {
                pixels = v.u.ba->data;
                pixelSize = v.u.ba->size;
            } else if (qstrcmp(key, "format") == 0 && v.format == MPV_FORMAT_STRING && v.u.string) {
                const char *f = v.u.string;
                if (qstrcmp(f, "bgr24") == 0)
                    fmt = QImage::Format_BGR888;
                else if (qstrcmp(f, "rgb24") == 0)
                    fmt = QImage::Format_RGB888;
                // libmpv commonly returns bgr0 (four bytes per pixel).
                // Treating it as 24-bit RGB made QImage consume a pixel
                // boundary every three bytes, which desaturated and
                // stretched the preview.
                else if (qstrcmp(f, "bgr0") == 0)
                    fmt = QImage::Format_RGB32;
                else if (qstrcmp(f, "bgra") == 0)
                    fmt = QImage::Format_ARGB32;
                else if (qstrcmp(f, "rgb0") == 0)
                    fmt = QImage::Format_RGBX8888;
                else if (qstrcmp(f, "rgba") == 0)
                    fmt = QImage::Format_RGBA8888;
                else if (qstrcmp(f, "bgrbgr") == 0 /* like bgr24 */)
                    fmt = QImage::Format_BGR888;
            }
        }
        if (pixels && width > 0 && height > 0 && fmt != QImage::Format_Invalid) {
            // Validate before narrowing or multiplying untrusted metadata.
            constexpr auto maxDimension = std::numeric_limits<int>::max();
            if (width > maxDimension || height > maxDimension || stride < 0 || stride > maxDimension)
                return {};
            // Determine per-pixel bytes for stride fallback (mpv gives
            // the stride; only 24/32bpp formats are produced here).
            int bpp = 32;
            if (fmt == QImage::Format_BGR888 || fmt == QImage::Format_RGB888)
                bpp = 24;
            const qint64 rowBytes = width * (bpp / 8);
            if (rowBytes > maxDimension)
                return {};
            if (stride == 0)
                stride = rowBytes;
            // Never create a QImage over a truncated frame.  Apart from
            // avoiding an out-of-bounds read, this prevents malformed
            // rows from appearing as a monochrome/stretched preview.
            const quint64 requiredBytes = static_cast<quint64>(stride) * height;
            if (stride < rowBytes || pixelSize < requiredBytes)
                return QImage();
            // The node owns the buffer: construct an image wrapping the
            // buffer then copy() so QImage owns the pixels after free.
            QImage wrapped(static_cast<const uchar *>(pixels), static_cast<int>(width), static_cast<int>(height),
                           stride, fmt);
            if (!bounds.isEmpty() && (wrapped.width() > bounds.width() || wrapped.height() > bounds.height()))
                return wrapped.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            image = wrapped.copy();
        }
    } else if (node.format == MPV_FORMAT_BYTE_ARRAY && node.u.ba) {
        if (!node.u.ba->data || node.u.ba->size > static_cast<size_t>(std::numeric_limits<int>::max()))
            return {};
        // Legacy engines return an encoded BMP. QImageReader may still need a
        // full decode internally; the raw-map path above never owns a full-size
        // intermediate. Borrow encoded bytes too, while the node is alive.
        QBuffer buffer;
        buffer.setData(QByteArray::fromRawData(static_cast<const char *>(node.u.ba->data),
                                             static_cast<int>(node.u.ba->size)));
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "BMP");
        if (!bounds.isEmpty() && reader.size().isValid())
            reader.setScaledSize(reader.size().boundedTo(reader.size().scaled(bounds, Qt::KeepAspectRatio)));
        image = reader.read();
        if (!image.isNull() && !bounds.isEmpty()
            && (image.width() > bounds.width() || image.height() > bounds.height()))
            image = image.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

QImage ThumbnailGenerator::imageFromScreenshotNode(const mpv_node &node)
{
    return decodeScreenshot(node);
}

QImage ThumbnailGenerator::thumbnailFromScreenshotNode(const mpv_node &node, QSize bounds)
{
    const QImage image = decodeScreenshot(node, boundedSize(bounds));
    return image.sizeInBytes() <= PosterImageCache::MaximumImageBytes ? image : QImage();
}

// Lives on the dedicated worker thread; owns the private mpv handle. The
// generator only ever talks to it through queued invocations, so no other
// thread touches m_mpv. Cross-thread writes only update atomics: request
// supersession is independent of permanent shutdown.
class ThumbnailWorker : public QObject {
    Q_OBJECT
public:
    ThumbnailWorker(ThumbnailMetadataLookup lookup, std::shared_ptr<PosterImageCache> posters)
        : m_lookup(std::move(lookup)), m_posters(std::move(posters))
    {
        setObjectName(QStringLiteral("ThumbnailWorker"));
        m_idleTimer = new QTimer(this);
        m_idleTimer->setSingleShot(true);
        connect(m_idleTimer, &QTimer::timeout, this, &ThumbnailWorker::shutdown);
    }

    ~ThumbnailWorker() override
    {
        // shutdown() already destroyed the handle on this thread; this is a
        // belt-and-braces guard for abnormal teardown paths.
        if (m_mpv)
            mpv_terminate_destroy(m_mpv);
    }

    void supersede(quint64 generation, quint64 reusableGeneration)
    {
        // Publish the reuse decision before the generation becomes obsolete.
        // This is only a same-path hint. The next capture validates metadata
        // on this thread before grabFrame can inherit a same-revision load.
        m_reusableGeneration = reusableGeneration;
        m_generation = generation;
    }
    void requestShutdown() { m_shutdown = true; }

    void discardPendingLoad(quint64 generation)
    {
        // A clear can arrive after capture decided to preserve a load but
        // before its completion reaches the GUI. Release that orphaned load,
        // unless a newer request is now going to use the worker.
        if (m_generation == generation && !m_loadingRevision.isEmpty())
            stopMedia();
    }

public slots:
    void inspectMetadata(quint64 generation, const QString &path)
    {
        const CaptureCancellation cancel{m_shutdown, m_generation, generation};
        const auto metadata = cancel.cancelled() ? ThumbnailMetadata() : m_lookup(path);
        if (!m_shutdown)
            emit metadataReady(generation, path, metadata);
    }

    void capture(quint64 generation, const QString &localPath, double seconds, int maxWidth,
                 bool cacheOnly, const QStringList &blockedKeys)
    {
        m_idleTimer->stop();
        QElapsedTimer elapsed;
        elapsed.start();
        const CaptureCancellation cancel{m_shutdown, m_generation, generation};
        const auto metadata = cancel.cancelled() ? ThumbnailMetadata() : m_lookup(localPath);
        QImage image;
        bool seekIssued = false;
        bool retryBlocked = false;
        if (!cancel.cancelled() && metadata.valid()) {
            if (seconds < 0)
                image = m_posters->image(posterKeyFor(metadata));
            retryBlocked = seconds < 0 && image.isNull() && blockedKeys.contains(posterKeyFor(metadata));
            if (image.isNull() && !cacheOnly && !retryBlocked
                && ensureInitialized(seconds < 0) && !cancel.cancelled())
                image = grabFrame(metadata.path, metadata.revision, seconds, maxWidth, cancel, seekIssued);
        }
        const bool cancelled = cancel.cancelled();
        if (cancelled || image.isNull()) {
            image = {};
            // Loading the same file for a newer timestamp is still useful.
            // stop can wait for decoder initialization before tearing it down,
            // then charge the latest request for that initialization again.
            // Preserve only pre-seek interactive loads: a cancelled seek still
            // stops so old restart events cannot satisfy the next capture.
            const bool reuseLoad = cancelled && !m_shutdown && !seekIssued
                && m_reusableGeneration == generation
                && metadata.valid()
                && (m_loadingRevision == metadata.revision || m_currentRevision == metadata.revision);
            if (!reuseLoad)
                stopMedia();
        }
        if (qEnvironmentVariableIsSet("FRAME_PERF"))
            qInfo("perf: %s capture %lldms %dx%d", seconds < 0 ? "poster" : "preview",
                  static_cast<long long>(elapsed.elapsed()), image.width(), image.height());
        if (!m_shutdown) {
            // Completion is required even for a superseded generation: it
            // releases the generator's active slot for the latest request.
            emit captureReady(generation, metadata, image, cancelled, retryBlocked);
            m_idleTimer->start(kIdleReleaseMs);
        }
    }

    void validateCapture(quint64 generation, const QString &localPath,
                         const ThumbnailMetadata &capturedMetadata, const QImage &image)
    {
        const CaptureCancellation cancel{m_shutdown, m_generation, generation};
        const auto metadata = cancel.cancelled() ? ThumbnailMetadata() : m_lookup(localPath);
        const bool cancelled = cancel.cancelled();
        const bool replaced = metadata != capturedMetadata;
        if (!cancelled && replaced)
            stopMedia();
        if (!m_shutdown) {
            emit captureValidated(generation, metadata, cancelled || replaced ? QImage() : image,
                                  cancelled, replaced);
            m_idleTimer->start(kIdleReleaseMs);
        }
    }

    void shutdown()
    {
        m_idleTimer->stop();
        if (m_mpv) {
            mpv_terminate_destroy(m_mpv);
            m_mpv = nullptr;
        }
        m_currentRevision.clear();
        m_loadingRevision.clear();
        m_loadStarted = false;
        m_loadTimer.invalidate();
    }

signals:
    void metadataReady(quint64 generation, const QString &path, const ThumbnailMetadata &metadata);
    void captureReady(quint64 generation, const ThumbnailMetadata &metadata, const QImage &image,
                      bool cancelled, bool retryBlocked);
    void captureValidated(quint64 generation, const ThumbnailMetadata &metadata, const QImage &image,
                          bool cancelled, bool replaced);

private:
    void stopMedia()
    {
        if (m_mpv) {
            const char *stop[] = {"stop", nullptr};
            mpv_command(m_mpv, stop);
        }
        m_currentRevision.clear();
        m_loadingRevision.clear();
        m_loadStarted = false;
        m_loadTimer.invalidate();
    }

    // Handles are created lazily on this thread, after moveToThread, so
    // mpv_initialize never races with the GUI thread's mpv instance.
    bool ensureInitialized(bool poster)
    {
        // Interactive 4K previews need more throughput than background art.
        // Recreate only when switching modes; there is still just one core.
        if (m_mpv && m_posterMode != poster)
            shutdown();
        if (m_mpv)
            return true;

        m_mpv = mpv_create();
        if (!m_mpv)
            return false;

        m_posterMode = poster;
        // Background art stays on a small software decoder. User-requested
        // previews may use copy-back hardware decoding: it returns CPU frames
        // without sharing Qt's OpenGL context and falls back to software.
        // pause=yes + keep-open=always park the core on the seeked frame: the
        // worker's mpv used to keep PLAYING the file at 1x after every capture
        // (full software decode of the remainder on one core, next to the
        // timing-sensitive main player).
        mpv_set_option_string(m_mpv, "vo", "null");
        mpv_set_option_string(m_mpv, "hwdec", poster ? "no" : "auto-copy");
        mpv_set_option_string(m_mpv, "audio", "no");
        mpv_set_option_string(m_mpv, "idle", "yes");
        mpv_set_option_string(m_mpv, "keep-open", "always");
        mpv_set_option_string(m_mpv, "pause", "yes");
        // One still image does not need playback's automatic 16-thread
        // decoder or its read-ahead buffers. In particular, 4K frame-thread
        // queues were retaining hundreds of MB after preparing the shelf.
        mpv_set_option_string(m_mpv, "vd-lavc-threads", poster ? "2" : "4");
        mpv_set_option_string(m_mpv, "cache", "no");
        mpv_set_option_string(m_mpv, "demuxer-max-bytes", "8MiB");
        mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "0");
        mpv_set_option_string(m_mpv, "demuxer-readahead-secs", "0");
        mpv_set_option_string(m_mpv, "load-scripts", "no");
        mpv_set_option_string(m_mpv, "terminal", "no");
        mpv_set_option_string(m_mpv, "config", "no");
        mpv_set_option_string(m_mpv, "input-default-bindings", "no");
        mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
        mpv_set_option_string(m_mpv, "input-cursor", "no");
        mpv_set_option_string(m_mpv, "net-fs", "no");
        // A local playlist/reference must not redirect this auxiliary core
        // into opening a network URL behind the local-path guard.
        mpv_set_option_string(m_mpv, "access-references", "no");
        mpv_set_option_string(m_mpv, "fullscreen", "no");
        mpv_set_option_string(m_mpv, "osd-level", "0");
        mpv_set_option_string(m_mpv, "osc", "no");
        // F44: this core exists to grab frames, never to render text, and
        // grabFrame() asks screenshot-raw for the bare video image. Leaving
        // libass's font providers on would make the worker enumerate every
        // installed font the first time the user hovers the seek bar — the
        // same ~180 MB / ~4000 handle tax the main core used to pay.
        mpv_set_option_string(m_mpv, "sub-font-provider", "none");
        mpv_set_option_string(m_mpv, "osd-font-provider", "none");
        mpv_set_option_string(m_mpv, "sub-auto", "no");

        if (mpv_initialize(m_mpv) < 0) {
            mpv_terminate_destroy(m_mpv);
            m_mpv = nullptr;
            return false;
        }
        return true;
    }

    QImage grabFrame(const QString &filePath, const QString &revision, double seconds, int maxWidth,
                     const CaptureCancellation &cancel, bool &seekIssued)
    {
        // Preserve START_FILE/FILE_LOADED events for a same-revision load
        // inherited from an obsolete capture. A different revision must stop
        // that load before starting and waiting for its own event pair.
        if (!m_loadingRevision.isEmpty() && m_loadingRevision != revision)
            stopMedia();
        if (m_loadingRevision.isEmpty())
            drainEvents(m_mpv, cancel);
        if (cancel.cancelled())
            return QImage();

        // Keep the media loaded between requests: a re-seek on the already
        // loaded file costs a fraction of the demuxer/decoder teardown and
        // re-init that a per-request loadfile pays.
        const QString normalized = QDir::fromNativeSeparators(filePath);
        if (revision != m_currentRevision) {
            if (m_loadingRevision.isEmpty()) {
                const QByteArray path = normalized.toUtf8();
                const char *loadArgs[] = {"loadfile", path.constData(), nullptr};
                m_loadingRevision = revision;
                m_loadStarted = false;
                m_loadTimer.start();
                if (mpv_command(m_mpv, loadArgs) < 0)
                    return {};
            }
            // Supersession does not restart the load deadline.
            const int remaining = int(qMax(qint64(0), kLoadTimeoutMs - m_loadTimer.elapsed()));
            if (!pumpUntilFileLoaded(m_mpv, remaining, cancel, m_loadStarted))
                return QImage();
            m_currentRevision = revision;
            m_loadingRevision.clear();
        }
        if (cancel.cancelled())
            return {};

        // A poster uses a representative frame beyond typical opening black.
        // Negative time is private to poster requests, keeping a seek to 0
        // distinct from an in-flight poster for the same file.
        const bool poster = seconds < 0;
        if (poster) {
            double duration = 0;
            mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &duration);
            seconds = duration > 0 ? qMin(30.0, duration * 0.08) : 0.0;
        }
        const QByteArray seekPos = QByteArray::number(seconds, 'f', 6);
        {
            // Artwork can use the nearby keyframe. Decoding every frame of
            // a long 4K GOP just for a card competes with interactive seeks.
            const char *args[] = {"seek", seekPos.constData(),
                                  poster ? "absolute+keyframes" : "absolute+exact", nullptr};
            seekIssued = true;
            if (mpv_command(m_mpv, args) < 0)
                return QImage();
        }

        if (!pumpUntilSeekLanded(m_mpv, seconds, kSeekTimeoutMs, cancel, !poster))
            return QImage();

        // PLAYBACK_RESTART plus the matching timestamp confirms the paused
        // frame is ready. A fixed extra sleep delayed every preview by 100ms.
        if (cancel.cancelled())
            return QImage();

        // F44: the "video" flag is explicit. screenshot-raw defaults to
        // "subtitles", which would drag libass (and a full system font
        // enumeration) into a seek-bar preview — and burned-in subtitles are
        // not wanted in a thumbnail anyway.
        mpv_node argStrings[2];
        argStrings[0].format = MPV_FORMAT_STRING;
        argStrings[0].u.string = const_cast<char *>("screenshot-raw");
        argStrings[1].format = MPV_FORMAT_STRING;
        argStrings[1].u.string = const_cast<char *>("video");

        mpv_node_list argsList;
        std::memset(&argsList, 0, sizeof(argsList));
        argsList.num = 2;
        argsList.values = argStrings;

        mpv_node argsNode;
        std::memset(&argsNode, 0, sizeof(argsNode));
        argsNode.format = MPV_FORMAT_NODE_ARRAY;
        argsNode.u.list = &argsList;

        mpv_node result{};
        if (mpv_command_node(m_mpv, &argsNode, &result) < 0)
            return QImage();

        const QImage image = cancel.cancelled() ? QImage()
            : ThumbnailGenerator::thumbnailFromScreenshotNode(result, QSize(maxWidth, maxWidth));
        mpv_free_node_contents(&result);
        return image;
    }

    const ThumbnailMetadataLookup m_lookup;
    const std::shared_ptr<PosterImageCache> m_posters;
    mpv_handle *m_mpv{nullptr};
    QTimer *m_idleTimer{nullptr};
    bool m_posterMode{true};
    QString m_currentRevision;
    QString m_loadingRevision;
    bool m_loadStarted{false};
    QElapsedTimer m_loadTimer;
    std::atomic<quint64> m_generation{0};
    std::atomic<quint64> m_reusableGeneration{0};
    std::atomic<bool> m_shutdown{false};
};

ThumbnailGenerator::ThumbnailGenerator(QObject *parent)
    : ThumbnailGenerator(lookupThumbnailMetadata, parent)
{
}

ThumbnailGenerator::ThumbnailGenerator(ThumbnailMetadataLookup lookup, QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ThumbnailMetadata>();
    m_clock.start();
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("ThumbnailGenThread"));

    auto *worker = new ThumbnailWorker(std::move(lookup), m_posters);
    m_worker = worker;
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &ThumbnailWorker::captureReady,
            this, &ThumbnailGenerator::onCaptureReady,
            Qt::QueuedConnection);
    connect(worker, &ThumbnailWorker::captureValidated,
            this, &ThumbnailGenerator::onCaptureValidated, Qt::QueuedConnection);
    connect(worker, &ThumbnailWorker::metadataReady, this,
            [this](quint64 generation, const QString &path, const ThumbnailMetadata &metadata) {
        if (!m_metadataActive || m_metadataActive->generation != generation
            || m_metadataActive->localPath != path)
            return;
        m_metadataActive.reset();
        if (generation == m_generation)
            rememberMetadata(path, metadata);
        startNextWork();
    }, Qt::QueuedConnection);
    m_thread->start(QThread::LowPriority);
}

ThumbnailGenerator::~ThumbnailGenerator()
{
    if (m_worker && m_thread) {
        auto *worker = static_cast<ThumbnailWorker *>(m_worker);
        // Cooperative cancellation interrupts our polling, not an OS metadata
        // call, mpv command or core destruction already in progress. Shutdown
        // joins this single worker safely; there is NO hard five-second bound.
        worker->requestShutdown();
        // quit() is thread-safe but does not drain queued work. Perform
        // cleanup and quit together on the worker so its timer and mpv
        // handle cannot fall through to destruction on the GUI thread.
        QMetaObject::invokeMethod(worker, [worker] {
            worker->shutdown();
            QThread::currentThread()->quit();
        }, Qt::QueuedConnection);
        if (!m_thread->wait(5000))
            m_thread->wait();
    }
    // QThread::finished processes the worker's deferred deletion before wait returns.
    m_worker = nullptr;
}

void ThumbnailGenerator::requestThumbnail(const QString &filePath, double seconds, int maxWidth)
{
    if (!m_worker)
        return;
    m_lastPreview.start();
    const auto path = localPathSyntax(filePath);
    if (!path || !std::isfinite(seconds) || seconds < 0) {
        clear();
        const quint64 generation = m_generation;
        // Existing callers expect a completion to release their busy state.
        // A subsequent valid request or clear suppresses this deferred failure.
        QTimer::singleShot(0, this, [this, generation, filePath, seconds] {
            if (generation == m_generation)
                emit thumbnailReady(filePath, seconds, {}, false);
        });
        return;
    }
    maxWidth = boundedSize(QSize(maxWidth, maxWidth)).width();
    const auto sameRequest = [&](const CaptureRequest &request) {
        return !request.poster && request.generation == m_generation && request.filePath == filePath
            && request.maxWidth == maxWidth && request.seconds == seconds;
    };
    if (m_captureActive && sameRequest(m_active))
        return;
    if (m_trailing && sameRequest(*m_trailing))
        return;

    const quint64 reusableGeneration = m_captureActive && !m_active.poster
        && metadataCacheKey(m_active.localPath) == metadataCacheKey(*path) ? m_active.generation : 0;
    advanceGeneration(reusableGeneration);
    const auto superseded = std::exchange(m_trailing, std::nullopt);
    m_trailing = CaptureRequest{filePath, *path, {}, seconds, maxWidth, m_generation, false};
    if (superseded && superseded->poster)
        emit posterCancelled(superseded->filePath, superseded->key);
    startNextWork();
}

void ThumbnailGenerator::advanceGeneration(quint64 reusableGeneration)
{
    ++m_generation;
    static_cast<ThumbnailWorker *>(m_worker)->supersede(m_generation, reusableGeneration);
}

void ThumbnailGenerator::dispatchCapture(const CaptureRequest &request)
{
    m_captureActive = true;
    m_active = request;
    // Unknown aliases can resolve to a revision whose retry budget is already
    // exhausted. Carry this bounded snapshot so resolution cannot bypass it.
    QStringList blockedKeys;
    if (request.poster) {
        for (const QString &key : m_failures.keys()) {
            const auto *failure = m_failures.object(key);
            if (failure->attempts >= kPosterAttempts || failure->retryAt > m_clock.elapsed())
                blockedKeys.append(key);
        }
    }
    QMetaObject::invokeMethod(
        m_worker,
        [worker = static_cast<ThumbnailWorker *>(m_worker), request, blockedKeys]() {
            worker->capture(request.generation, request.localPath, request.seconds, request.maxWidth,
                            request.cacheOnly, blockedKeys);
        },
        Qt::QueuedConnection);
}

bool ThumbnailGenerator::requestPoster(const QString &filePath)
{
    const auto path = localPathSyntax(filePath);
    if (!path || m_captureActive || m_trailing)
        return false;
    const QString key = posterKey(filePath);
    const auto *known = m_metadata.object(metadataCacheKey(*path));
    if (known && (!known->media.valid() || posterRetryDelay(filePath) != 0)) {
        // A cached failure is not evidence that a replacement is still bad.
        refreshMetadata(filePath);
        return false;
    }
    const bool cacheOnly = m_backgroundSuspended
        || (m_lastPreview.isValid() && m_lastPreview.elapsed() < kPreviewQuietMs)
        || (m_lastPoster.isValid() && m_lastPoster.elapsed() < (m_playbackActive ? 1200 : 250));
    if (cacheOnly && !m_posters->contains(key))
        return false;
    advanceGeneration();
    // A metadata scan must not starve capture by immediately starting another
    // scan at every completion. Reserve the one trailing slot behind it; an
    // interactive request can still replace this poster before it starts.
    m_trailing = CaptureRequest{filePath, *path, key, -1.0, kPosterDimension, m_generation, true, cacheOnly};
    startNextWork();
    return true;
}

void ThumbnailGenerator::clear()
{
    advanceGeneration();
    const auto cancelled = std::exchange(m_trailing, std::nullopt);
    m_metadataQueue.clear();
    if (cancelled && cancelled->poster)
        emit posterCancelled(cancelled->filePath, cancelled->key);
    emit previewInvalidated();
}

void ThumbnailGenerator::refreshMetadata(const QString &filePath, bool force)
{
    const auto path = localPathSyntax(filePath);
    if (!path)
        return;
    const QString key = metadataCacheKey(*path);
    const auto *known = m_metadata.object(key);
    if (!force && known && m_clock.elapsed() - known->checkedAt < 1000)
        return;
    if ((m_metadataActive && m_metadataActive->generation == m_generation
            && metadataCacheKey(m_metadataActive->localPath) == key)
        || (m_captureActive && m_active.generation == m_generation
            && metadataCacheKey(m_active.localPath) == key))
        return;
    for (const auto &pending : std::as_const(m_metadataQueue)) {
        if (metadataCacheKey(pending.localPath) == key)
            return;
    }
    // Queue only names, not worker invocations/threads for each playlist row.
    // The shelf's periodic pass can retry anything dropped at this bound.
    if (m_metadataQueue.size() == 64)
        m_metadataQueue.removeFirst();
    m_metadataQueue.append(MetadataRequest{*path, 0});
    startNextWork();
}

void ThumbnailGenerator::startNextWork()
{
    if (m_captureActive || m_metadataActive)
        return;
    if (m_trailing) {
        const auto request = std::exchange(m_trailing, std::nullopt);
        if (request->generation == m_generation) {
            dispatchCapture(*request);
            return;
        }
    }
    if (!m_backgroundSuspended && !m_metadataQueue.isEmpty()) {
        auto request = m_metadataQueue.takeFirst();
        request.generation = m_generation;
        m_metadataActive = request;
        QMetaObject::invokeMethod(m_worker, [worker = static_cast<ThumbnailWorker *>(m_worker), request] {
            worker->inspectMetadata(request.generation, request.localPath);
        }, Qt::QueuedConnection);
    }
}

void ThumbnailGenerator::rememberMetadata(const QString &localPath, const ThumbnailMetadata &metadata)
{
    const QString key = metadataCacheKey(localPath);
    auto *previous = m_metadata.object(key);
    if (previous && previous->media == metadata) {
        previous->checkedAt = m_clock.elapsed();
        return;
    }
    const QString previousIdentity = previous ? previous->media.identity : QString();
    // An alias observed at an older revision is no longer a valid cached
    // answer. Revalidate it independently: a symlink might also be retargeted.
    for (const QString &alias : m_metadata.keys()) {
        const auto *known = m_metadata.object(alias);
        if (alias != key && known->media.valid()
            && ((known->media.identity == metadata.identity && known->media.revision != metadata.revision)
                || (!metadata.valid() && known->media.identity == previousIdentity)))
            m_metadata.remove(alias);
    }
    const QString posterKey = posterKeyFor(metadata);
    m_metadata.insert(key, new MetadataObservation{metadata, posterKey, m_clock.elapsed()});
    if (metadata.valid())
        evictOtherRevisions(metadata.identity, posterKey);
    else if (!previousIdentity.isEmpty())
        evictOtherRevisions(previousIdentity, {});
    emit metadataChanged();
}

bool ThumbnailGenerator::canPreview(const QString &filePath) const
{
    const auto path = localPathSyntax(filePath);
    const auto *metadata = path ? m_metadata.object(metadataCacheKey(*path)) : nullptr;
    return metadata && metadata->media.valid();
}

QString ThumbnailGenerator::posterKey(const QString &filePath) const
{
    const auto path = localPathSyntax(filePath);
    const auto *metadata = path ? m_metadata.object(metadataCacheKey(*path)) : nullptr;
    return metadata ? metadata->posterKey : QString();
}

int ThumbnailGenerator::posterRetryDelay(const QString &filePath) const
{
    const QString key = posterKey(filePath);
    if (key.isEmpty())
        return -1;
    const Failure *failure = m_failures.object(key);
    if (!failure)
        return 0;
    if (failure->attempts >= kPosterAttempts)
        return -1;
    return int(qBound(qint64(0), failure->retryAt - m_clock.elapsed(), qint64(2000)));
}

bool ThumbnailGenerator::posterCached(const QString &key) const
{
    return m_posters->contains(key);
}

void ThumbnailGenerator::setPlaybackPressure(bool firstPicturePending, bool playbackActive, bool playbackBusy)
{
    const bool suspended = firstPicturePending || playbackBusy;
    if (m_backgroundSuspended == suspended && m_playbackActive == playbackActive)
        return;
    m_backgroundSuspended = suspended;
    m_playbackActive = playbackActive;
    if (suspended) {
        if ((m_captureActive && m_active.poster && m_active.generation == m_generation)
            || (m_trailing && m_trailing->poster && m_trailing->generation == m_generation))
            advanceGeneration();
        if (m_trailing && m_trailing->poster) {
            const auto cancelled = std::exchange(m_trailing, std::nullopt);
            emit posterCancelled(cancelled->filePath, cancelled->key);
        }
    }
    emit posterSchedulingChanged();
    startNextWork();
}

void ThumbnailGenerator::evictOtherRevisions(const QString &identity, const QString &key)
{
    const auto evicted = m_posters->removeOtherRevisions(identity, key);
    for (const QString &oldKey : evicted)
        emit posterEvicted(oldKey);
}

void ThumbnailGenerator::onCaptureReady(quint64 generation, const ThumbnailMetadata &metadata,
                                         const QImage &image, bool cancelled, bool retryBlocked)
{
    if (!m_captureActive || generation != m_active.generation)
        return;
    m_active.retryBlocked = retryBlocked;
    if (cancelled || generation != m_generation) {
        onCaptureValidated(generation, {}, {}, true, false);
        return;
    }
    // Keep the active slot until validation returns. In particular, a file
    // replaced while the candidate was queued on a busy GUI must be checked
    // again, on the worker, before the GUI can publish/cache those pixels.
    QMetaObject::invokeMethod(m_worker,
        [worker = static_cast<ThumbnailWorker *>(m_worker), generation, path = m_active.localPath, metadata, image] {
            worker->validateCapture(generation, path, metadata, image);
        }, Qt::QueuedConnection);
}

void ThumbnailGenerator::onCaptureValidated(quint64 generation, const ThumbnailMetadata &metadata,
                                             const QImage &image, bool cancelled, bool replaced)
{
    if (!m_captureActive || generation != m_active.generation)
        return;
    const CaptureRequest completed = m_active;
    m_captureActive = false;
    m_active = {};
    if (!cancelled && generation == m_generation)
        rememberMetadata(completed.localPath, metadata);
    // Metadata/eviction handlers can synchronously clear or issue a request.
    const bool obsolete = cancelled || generation != m_generation;
    const QImage captured = obsolete || replaced ? QImage() : image;
    const QString key = posterKeyFor(metadata);

    if (completed.poster) {
        if (obsolete || replaced || !metadata.valid() || completed.retryBlocked
            || (completed.cacheOnly && captured.isNull())) {
            // Cancellation/replacement is not a decode failure and must not
            // consume the transient-failure budget or leave QML pending.
            emit posterCancelled(completed.filePath, completed.key);
        } else {
            if (captured.isNull() || !m_posters->contains(key))
                m_lastPoster.start();
            if (captured.isNull()) {
                const Failure *previous = m_failures.object(key);
                const int attempts = previous ? previous->attempts + 1 : 1;
                const int backoff = attempts == 1 ? 500 : 2000;
                m_failures.insert(key, new Failure{attempts, m_clock.elapsed() + backoff});
            } else {
                m_failures.remove(key);
                const auto evicted = m_posters->insert(key, metadata.identity, captured);
                for (const QString &key : evicted)
                    emit posterEvicted(key);
            }
            if (generation == m_generation)
                emit posterReady(completed.filePath, key, captured, !captured.isNull());
            else
                emit posterCancelled(completed.filePath, completed.key);
        }
    } else if (!obsolete) {
        m_lastPreview.start();
        emit thumbnailReady(completed.filePath, completed.seconds, captured, !captured.isNull());
    }
    startNextWork();
    if (!m_captureActive && !m_trailing) {
        QMetaObject::invokeMethod(m_worker,
            [worker = static_cast<ThumbnailWorker *>(m_worker), latest = m_generation] {
                worker->discardPendingLoad(latest);
            }, Qt::QueuedConnection);
    }
}

#include "ThumbnailGenerator.moc"
