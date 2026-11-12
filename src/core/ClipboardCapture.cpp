#include "core/ClipboardCapture.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QMutex>
#include <QPointer>
#include <QThread>
#include <QWaitCondition>

#include <limits>
#include <optional>
#include <utility>

#include <mpv/client.h>

namespace {

// screenshot-raw is explicitly requested as rgba, so no encoded image or
// native-endian pixel-format guesses are needed. The result is borrowed from
// mpv's event queue, including its byte array; only the copy may escape.
QImage copyRgba(const mpv_node &node)
{
    if (node.format != MPV_FORMAT_NODE_MAP || !node.u.list)
        return {};
    const auto &list = *node.u.list;
    if (list.num <= 0 || !list.keys || !list.values)
        return {};

    qint64 width = 0, height = 0, stride = 0;
    const mpv_byte_array *bytes = nullptr;
    bool rgba = false;
    for (int i = 0; i < list.num; ++i) {
        const char *key = list.keys[i];
        const auto &value = list.values[i];
        if (!key)
            continue;
        if (qstrcmp(key, "w") == 0 && value.format == MPV_FORMAT_INT64)
            width = value.u.int64;
        else if (qstrcmp(key, "h") == 0 && value.format == MPV_FORMAT_INT64)
            height = value.u.int64;
        else if (qstrcmp(key, "stride") == 0 && value.format == MPV_FORMAT_INT64)
            stride = value.u.int64;
        else if (qstrcmp(key, "format") == 0 && value.format == MPV_FORMAT_STRING)
            rgba = value.u.string && qstrcmp(value.u.string, "rgba") == 0;
        else if (qstrcmp(key, "data") == 0 && value.format == MPV_FORMAT_BYTE_ARRAY)
            bytes = value.u.ba;
    }

    constexpr auto maxInt = std::numeric_limits<int>::max();
    if (!rgba || !bytes || !bytes->data || width <= 0 || height <= 0
        || width > maxInt / 4 || height > maxInt || stride < width * 4 || stride > maxInt)
        return {};
    const auto size = static_cast<quint64>(stride) * static_cast<quint64>(height);
    if (size > static_cast<quint64>(std::numeric_limits<qsizetype>::max())
        || size > bytes->size)
        return {};

    return QImage(static_cast<const uchar *>(bytes->data), static_cast<int>(width),
                  static_cast<int>(height), static_cast<qsizetype>(stride),
                  QImage::Format_RGBA8888).copy();
}

} // namespace

struct ClipboardCapture::Impl {
    enum class Kind { Frame, File };
    struct Request {
        Kind kind;
        quint64 epoch;
        bool includeSubtitles = false;
        QString path;
    };

    ClipboardCapture *const owner;
    mpv_handle *mpv;
    std::function<quint64()> nextReplyId;
    quint64 epoch = 0;
    bool closed = false;
    std::optional<Request> active;
    std::optional<Request> pending;
    std::optional<quint64> replyId;

    // Only this mailbox is shared with the reader. All intent/active/reply
    // state above stays on the GUI thread, including completion delivery.
    QMutex readerMutex;
    QWaitCondition readerReady;
    bool readerStopping = false;
    std::optional<Request> readerJob;
    std::unique_ptr<QThread> reader;

    Impl(ClipboardCapture *owner, mpv_handle *mpv, std::function<quint64()> nextReplyId)
        : owner(owner), mpv(mpv), nextReplyId(std::move(nextReplyId)) {}

    void assertThread() const
    {
        Q_ASSERT(QThread::currentThread() == owner->thread());
        Q_ASSERT(QGuiApplication::instance()
                 && owner->thread() == QGuiApplication::instance()->thread());
    }

    void runReader()
    {
        for (;;) {
            std::optional<Request> job;
            {
                QMutexLocker lock(&readerMutex);
                while (!readerStopping && !readerJob)
                    readerReady.wait(&readerMutex);
                if (readerStopping)
                    return;
                job = std::move(readerJob);
                readerJob.reset();
            }

            QImage image(job->path);
            {
                QMutexLocker lock(&readerMutex);
                if (readerStopping)
                    return;
            }
            // close() joins this worker before owner/Impl can be destroyed.
            // A racing post is therefore safe; the receiver context removes
            // undelivered callbacks on destruction. Never check a QPointer
            // on this thread and then race its destruction while posting.
            QMetaObject::invokeMethod(owner,
                [receiver = owner, token = job->epoch, image = std::move(image)]() mutable {
                    receiver->m_impl->complete(token, std::move(image));
                }, Qt::QueuedConnection);
        }
    }

    void submitNext()
    {
        if (closed || active || !pending)
            return;
        active = std::move(pending);
        pending.reset();

        if (active->kind == Kind::Frame) {
            const char *args[] = {"screenshot-raw",
                                 active->includeSubtitles ? "subtitles" : "video",
                                 "rgba", nullptr};
            replyId = nextReplyId();
            if (mpv_command_async(mpv, *replyId, args) < 0) {
                replyId.reset(); // No event is generated on enqueue failure.
                QMetaObject::invokeMethod(owner,
                    [receiver = owner, token = active->epoch] {
                        receiver->m_impl->complete(token, {});
                    }, Qt::QueuedConnection);
            }
            return;
        }

        if (!reader) {
            reader.reset(QThread::create([this] { runReader(); }));
            reader->setObjectName(QStringLiteral("ClipboardCaptureReader"));
            reader->start(QThread::LowPriority);
        }
        {
            QMutexLocker lock(&readerMutex);
            readerJob = *active;
        }
        readerReady.wakeOne();
    }

    void complete(quint64 token, QImage image)
    {
        assertThread();
        if (!active || active->epoch != token)
            return;
        const Request completed = std::move(*active);
        active.reset();
        if (closed || token != epoch) {
            submitNext();
            return;
        }

        // A current completion cannot have a newer pending request. Make no
        // further member accesses after emitting: consumers may delete us.
        QPointer<ClipboardCapture> guard(owner);
        QClipboard *clipboard = QGuiApplication::clipboard();
        const bool success = clipboard && !image.isNull();
        if (success)
            clipboard->setImage(image);
        // setImage can synchronously emit dataChanged, whose handlers may
        // invalidate, enqueue another request, close, or delete this helper.
        if (guard && !guard->m_impl->closed && token == guard->m_impl->epoch)
            emit guard->finished(completed.path, success);
    }
};

ClipboardCapture::ClipboardCapture(mpv_handle *mpv, std::function<quint64()> nextReplyId,
                                   QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(this, mpv, std::move(nextReplyId)))
{
    m_impl->assertThread();
}

ClipboardCapture::~ClipboardCapture()
{
    close();
}

bool ClipboardCapture::requestFrame(bool includeSubtitles)
{
    m_impl->assertThread();
    if (m_impl->closed || !m_impl->mpv || !m_impl->nextReplyId)
        return false;
    m_impl->pending = Impl::Request{Impl::Kind::Frame, ++m_impl->epoch, includeSubtitles, {}};
    m_impl->submitNext();
    return true;
}

void ClipboardCapture::requestFile(const QString &path)
{
    m_impl->assertThread();
    if (m_impl->closed)
        return;
    m_impl->pending = Impl::Request{Impl::Kind::File, ++m_impl->epoch, false, path};
    m_impl->submitNext();
}

void ClipboardCapture::invalidate()
{
    m_impl->assertThread();
    ++m_impl->epoch;
    m_impl->pending.reset();
}

void ClipboardCapture::close()
{
    m_impl->assertThread();
    if (m_impl->closed)
        return;
    invalidate();
    m_impl->closed = true;
    m_impl->mpv = nullptr;
    {
        QMutexLocker lock(&m_impl->readerMutex);
        m_impl->readerStopping = true;
        m_impl->readerJob.reset();
    }
    if (m_impl->reader) {
        m_impl->readerReady.wakeOne();
        m_impl->reader->wait();
        m_impl->reader.reset();
    }
    m_impl->active.reset();
    // Retain replyId so the parent can still route/discard our raw reply.
}

bool ClipboardCapture::handleEvent(const mpv_event &event)
{
    m_impl->assertThread();
    if (!m_impl->replyId || event.event_id != MPV_EVENT_COMMAND_REPLY
        || event.reply_userdata != *m_impl->replyId)
        return false;
    m_impl->replyId.reset();
    if (!m_impl->active)
        return true; // Closed while the raw request was in flight.

    const quint64 token = m_impl->active->epoch;
    QImage image;
    if (!m_impl->closed && token == m_impl->epoch && event.error >= 0 && event.data) {
        const auto *reply = static_cast<const mpv_event_command *>(event.data);
        image = copyRgba(reply->result);
    }
    m_impl->complete(token, std::move(image));
    return true;
}
