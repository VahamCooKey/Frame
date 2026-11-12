#include "core/AudioOverview.h"
#include "core/ThumbnailGenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QtMath>
#include <cmath>
#include <mpv/client.h>

namespace {

QString takeString(mpv_handle *mpv, const char *name)
{
    char *value = mpv_get_property_string(mpv, name);
    if (!value) return {};
    const QString text = QString::fromUtf8(value).trimmed();
    mpv_free(value);
    return text;
}

void setOptions(mpv_handle *mpv, std::initializer_list<std::pair<const char *, const char *>> options)
{
    for (const auto &[key, value] : options)
        mpv_set_option_string(mpv, key, value);
}

void setCommonOptions(mpv_handle *mpv)
{
    setOptions(mpv, {{"config", "no"}, {"load-scripts", "no"}, {"terminal", "no"},
                     {"input-default-bindings", "no"}, {"osd-level", "0"},
                     {"resume-playback", "no"}, {"save-position-on-quit", "no"},
                     {"sub-auto", "no"}, {"sid", "no"}, {"cache", "no"},
                     {"ytdl", "no"}, {"access-references", "no"}});
}

// Waits for the wanted event. false on timeout, cancel, END_FILE or SHUTDOWN.
template <typename Cancelled>
bool waitFor(mpv_handle *mpv, mpv_event_id wanted, int timeoutMs, Cancelled cancelled)
{
    QElapsedTimer timer;
    timer.start();
    while (!cancelled() && timer.elapsed() < timeoutMs) {
        const mpv_event *event = mpv_wait_event(mpv, 0.05);
        if (event->event_id == wanted) return true;
        if (event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_SHUTDOWN)
            return false;
    }
    return false;
}

QString localPath(const QString &path)
{
    if (path.isEmpty()) return {};
    const QUrl url(path);
    if (url.isLocalFile()) return url.toLocalFile();
    // A drive letter parses as a one-letter scheme; anything else with a
    // scheme (http, ytdl, ...) is not a local file.
    if (path.contains(QStringLiteral("://"))) return {};
    return path;
}

} // namespace

AudioOverview::AudioOverview(QObject *parent) : QObject(parent) {}

AudioOverview::~AudioOverview() { cancelWorker(); }

void AudioOverview::cancelWorker()
{
    ++m_generation;
    if (m_worker.joinable()) m_worker.join();
}

void AudioOverview::clear()
{
    cancelWorker();
    if (m_path.isEmpty() && m_peaks.isEmpty() && m_coverSource.isEmpty()) return;
    m_path.clear(); m_title.clear(); m_artist.clear(); m_album.clear(); m_coverSource.clear();
    m_peaks.clear();
    m_cover->set({});
    emit changed();
    emit peaksChanged();
}

void AudioOverview::load(const QString &path)
{
    if (path == m_path) return;
    const QString local = localPath(path);
    if (local.isEmpty() || !QFileInfo(local).isFile()) { clear(); return; }
    clear();
    m_path = path;
    emit changed();
    const quint64 generation = ++m_generation;
    m_worker = std::thread([this, generation, local] { run(generation, local); });
}

void AudioOverview::run(quint64 generation, QString path)
{
    QImage cover;
    const Tags tags = readTagsAndCover(generation, path, cover);
    if (cancelled(generation)) return;
    QMetaObject::invokeMethod(this, [this, generation, tags, cover] {
        if (cancelled(generation)) return;
        m_title = tags.title; m_artist = tags.artist; m_album = tags.album;
        m_cover->set(cover);
        m_coverSource = cover.isNull() ? QString()
                                       : QStringLiteral("image://audiocover/%1").arg(++m_coverSerial);
        emit changed();
    }, Qt::QueuedConnection);

    const QVariantList peaks = decodePeaks(generation, path, tags.duration);
    if (cancelled(generation)) return;
    QMetaObject::invokeMethod(this, [this, generation, peaks] {
        if (cancelled(generation)) return;
        m_peaks = peaks;
        emit peaksChanged();
    }, Qt::QueuedConnection);
}

AudioOverview::Tags AudioOverview::readTagsAndCover(quint64 generation, const QString &path, QImage &cover)
{
    Tags tags;
    mpv_handle *mpv = mpv_create();
    if (!mpv) return tags;
    setCommonOptions(mpv);
    // ao=null rather than audio=no: a file without a picture would have no
    // stream left and end before FILE_LOADED. vo=null still decodes the
    // cover so screenshot-raw can read it; pause
    // + keep-open park the core on that single frame.
    setOptions(mpv, {{"vo", "null"}, {"ao", "null"}, {"pause", "yes"}, {"keep-open", "always"},
                     {"idle", "yes"}, {"hwdec", "no"}, {"vd-lavc-threads", "1"}});
    const auto stop = [&] { return cancelled(generation); };
    if (mpv_initialize(mpv) >= 0) {
        const QByteArray file = QDir::fromNativeSeparators(path).toUtf8();
        const char *args[] = {"loadfile", file.constData(), nullptr};
        if (mpv_command(mpv, args) >= 0 && waitFor(mpv, MPV_EVENT_FILE_LOADED, 8000, stop)) {
            tags.title = takeString(mpv, "metadata/by-key/title");
            tags.artist = takeString(mpv, "metadata/by-key/artist");
            if (tags.artist.isEmpty()) tags.artist = takeString(mpv, "metadata/by-key/album_artist");
            tags.album = takeString(mpv, "metadata/by-key/album");
            mpv_get_property(mpv, "duration", MPV_FORMAT_DOUBLE, &tags.duration);
            const bool hasPicture = !takeString(mpv, "current-tracks/video/id").isEmpty();
            if (hasPicture && waitFor(mpv, MPV_EVENT_PLAYBACK_RESTART, 5000, stop)) {
                const char *shot[] = {"screenshot-raw", "video", nullptr};
                mpv_node result{};
                if (mpv_command_ret(mpv, shot, &result) >= 0) {
                    cover = ThumbnailGenerator::thumbnailFromScreenshotNode(result, QSize(800, 800));
                    mpv_free_node_contents(&result);
                }
            }
        }
    }
    mpv_terminate_destroy(mpv);
    return tags;
}

QVariantList AudioOverview::decodePeaks(quint64 generation, const QString &path, double duration)
{
    // Very long recordings are skipped rather than decoded for minutes.
    if (duration > 6 * 3600) return {};
    // Mono s16 at a rate that keeps the scratch file near 4 MB; a waveform
    // of a few hundred bars does not need more.
    const int rate = duration > 0 ? qBound(1000, int(2000000 / duration), 8000) : 4000;
    const QString scratch = QDir::temp().filePath(
        QStringLiteral("frame-waveform-%1-%2.pcm").arg(QCoreApplication::applicationPid()).arg(generation));
    QVariantList peaks;
    mpv_handle *mpv = mpv_create();
    if (!mpv) return peaks;
    setCommonOptions(mpv);
    const QByteArray scratchPath = QDir::toNativeSeparators(scratch).toUtf8();
    const QByteArray rateText = QByteArray::number(rate);
    // ao=pcm is untimed: it decodes as fast as the CPU allows.
    setOptions(mpv, {{"vid", "no"}, {"vo", "null"}, {"ao", "pcm"}, {"ao-pcm-waveheader", "no"},
                     {"audio-format", "s16"}, {"audio-channels", "mono"}, {"idle", "no"},
                     {"cover-art-auto", "no"}, {"audio-display", "no"}});
    mpv_set_option_string(mpv, "ao-pcm-file", scratchPath.constData());
    mpv_set_option_string(mpv, "audio-samplerate", rateText.constData());
    const auto stop = [&] { return cancelled(generation); };
    bool finished = false;
    if (mpv_initialize(mpv) >= 0) {
        const QByteArray file = QDir::fromNativeSeparators(path).toUtf8();
        const char *args[] = {"loadfile", file.constData(), nullptr};
        if (mpv_command(mpv, args) >= 0) {
            QElapsedTimer timer;
            timer.start();
            while (!stop() && timer.elapsed() < 120000) {
                const mpv_event *event = mpv_wait_event(mpv, 0.05);
                if (event->event_id == MPV_EVENT_END_FILE) {
                    const auto *end = static_cast<const mpv_event_end_file *>(event->data);
                    finished = end && end->reason == MPV_END_FILE_REASON_EOF;
                    break;
                }
                if (event->event_id == MPV_EVENT_SHUTDOWN) break;
            }
        }
    }
    mpv_terminate_destroy(mpv);
    QFile pcm(scratch);
    if (finished && !stop() && pcm.open(QIODevice::ReadOnly)) {
        const QByteArray data = pcm.readAll();
        peaks = peaksFromSamples(reinterpret_cast<const qint16 *>(data.constData()),
                                 data.size() / qsizetype(sizeof(qint16)), PeakCount);
        pcm.close();
    }
    QFile::remove(scratch);
    return peaks;
}

QVariantList AudioOverview::peaksFromSamples(const qint16 *samples, qsizetype count, int buckets)
{
    QVariantList peaks;
    if (!samples || count <= 0 || buckets <= 0) return peaks;
    buckets = int(qMin<qsizetype>(buckets, count));
    QList<int> raw(buckets, 0);
    int loudest = 0;
    for (int b = 0; b < buckets; ++b) {
        const qsizetype from = count * b / buckets, to = count * (b + 1) / buckets;
        int peak = 0;
        for (qsizetype i = from; i < to; ++i)
            peak = qMax(peak, qAbs(int(samples[i])));
        raw[b] = peak;
        loudest = qMax(loudest, peak);
    }
    peaks.reserve(buckets);
    for (const int peak : raw)
        // A mild curve keeps quiet passages visible next to loud ones.
        peaks.append(loudest > 0 ? std::pow(double(peak) / loudest, 0.8) : 0.0);
    return peaks;
}
