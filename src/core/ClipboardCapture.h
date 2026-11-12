#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

struct mpv_handle;
struct mpv_event;

// Construct, call and destroy on the GUI thread. The caller owns mpv and its
// event loop: pass events to handleEvent() before the next mpv_wait_event(),
// and close/destroy this helper before destroying the handle.
//
// Raw captures and file reads share one active operation and one replaceable
// pending request. Superseded/invalidated requests finish silently. File I/O
// runs on one lazily started, persistent worker; only the GUI touches mpv or
// the clipboard. No dependency on ThumbnailGenerator or QtConcurrent.
class ClipboardCapture final : public QObject {
    Q_OBJECT
public:
    // nextReplyId must allocate IDs shared with every other async mpv caller.
    // Zero is a valid ID; IDs must not be reused while replies are outstanding.
    explicit ClipboardCapture(mpv_handle *mpv, std::function<quint64()> nextReplyId,
                              QObject *parent = nullptr);
    ~ClipboardCapture() override;

    // True means accepted, including a coalesced trailing request, NOT copied.
    // False means closed, or no mpv handle/ID allocator. Enqueue errors are
    // reported asynchronously through finished(), if the request is current.
    bool requestFrame(bool includeSubtitles);
    void requestFile(const QString &path);

    // Suppress active results and discard pending work on stop/file change.
    // Keep outstanding work accounted for until its reply/completion arrives.
    void invalidate();

    // Idempotent; rejects further requests and joins at most one file reader.
    // An in-progress QImage load cannot be interrupted, so slow storage can
    // delay the join. No mpv wait or abort is performed here.
    void close();

    // True only for this helper's outstanding raw reply (also after close).
    // Borrowed event pixels are copied before returning and are never freed.
    bool handleEvent(const mpv_event &event);

signals:
    // Emitted on the GUI thread after decoding/capture and QClipboard::setImage,
    // or on failure. Empty path denotes raw capture. No success on submission.
    // Qt does not expose a native clipboard-write acknowledgement/error here.
    void finished(const QString &path, bool success);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
