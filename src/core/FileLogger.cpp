#include "FileLogger.h"
#include "LogSanitizer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace {
thread_local bool inHandler = false;
thread_local bool inWriter = false;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtWarningMsg: return "WARN";
    case QtCriticalMsg: return "CRIT";
    case QtFatalMsg: return "FATAL";
    default: return "INFO";
    }
}

QByteArray formatRecord(QtMsgType type, const char *category, const QString &message)
{
    return QStringLiteral("[%1] %2 %3: %4\n").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
        QString::fromLatin1(levelName(type)),
        LogSanitizer::sanitize(QString::fromUtf8(category ? category : "default")),
        LogSanitizer::sanitize(message)).toUtf8();
}

// This tiny dispatch slot deliberately lives until process exit. Qt/plugin
// static destructors can still log after our worker has been drained. They
// see an empty slot instead of a destroyed FileLogger or destroyed mutex.
struct Dispatch {
    std::mutex mutex;
    std::condition_variable idle;
    std::shared_ptr<FileLogger> logger;
    size_t handlers{0};
};

Dispatch &installedLogger()
{
    static auto *slot = new Dispatch;
    return *slot;
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (inHandler || inWriter) {
        // Never recursively format or enqueue a QFile/formatter diagnostic.
        std::fputs("Frame logger: recursive diagnostic suppressed\n", stderr);
        return;
    }
    inHandler = true;
    auto &dispatch = installedLogger();
    std::shared_ptr<FileLogger> logger;
    {
        std::lock_guard lock(dispatch.mutex);
        logger = dispatch.logger;
        if (logger) ++dispatch.handlers;
    }
    if (logger) logger->submit(type, context.category, message);
    if (logger && type == QtFatalMsg) {
        const QByteArray safe = formatRecord(type, context.category, message);
        // Fatal abort cannot await a stuck disk. stderr is best effort and
        // can itself block; accepted file records are not an fsync guarantee.
        std::fwrite(safe.constData(), 1, size_t(safe.size()), stderr);
        std::fflush(stderr);
    } else if (!logger) {
        // No Qt formatting or sanitizer statics after their shutdown. Do
        // not expose the original, potentially sensitive diagnostic here.
        std::fputs("Frame logger: diagnostic after shutdown suppressed\n", stderr);
    }
    if (logger) {
        std::lock_guard lock(dispatch.mutex);
        if (--dispatch.handlers == 0) dispatch.idle.notify_all();
    }
    inHandler = false;
}

struct Installation {
    std::shared_ptr<FileLogger> logger;
    ~Installation()
    {
        FileLogger::shutdownInstalled();
        logger->shutdown();
    }
};
}

struct FileLogger::State {
    Options options;
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<QByteArray> queue;
    qsizetype bytes{0};
    std::array<quint64, 5> dropped{};
    bool stopping{false};
    std::thread worker;

    explicit State(Options o) : options(std::move(o))
    {
        options.maxRecords = qMax(qsizetype(1), options.maxRecords);
        options.maxBytes = qMax(qsizetype(1), options.maxBytes);
        options.rotationBytes = qMax(qint64(1), options.rotationBytes);
    }

    void run()
    {
        inWriter = true;
        QFile file(options.path);
        bool ioFailureReported = false;
        const auto reportFailure = [&] {
            if (!ioFailureReported)
                std::fputs("Frame logger: log storage failed; records may be lost\n", stderr);
            ioFailureReported = true;
        };
        const auto open = [&] {
            return file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        };
        if (!options.sink && !open()) {
            file.setFileName(QDir::tempPath() + QStringLiteral("/frame.log"));
            if (!open()) reportFailure();
        }
        const auto write = [&](const QByteArray &line) {
            if (options.sink) { options.sink(line); return; }
            if (!file.isOpen() && !open()) { reportFailure(); return; }
            if (file.size() > 0 && file.size() + line.size() > options.rotationBytes) {
                const QString base = file.fileName();
                file.close();
                const QString backup = base + QStringLiteral(".1");
                // If a backup is locked/obstructed, retain the old log and
                // drop this record. Never reopen an oversized file in append
                // mode after failed rotation and let it grow without bound.
                if ((QFile::exists(backup) && !QFile::remove(backup))
                    || !QFile::rename(base, backup)) {
                    reportFailure();
                    return;
                }
                if (!open()) { reportFailure(); return; }
            }
            if (file.write(line) != line.size()) reportFailure();
        };
        if (options.announcePath && !options.sink && file.isOpen())
            write(formatRecord(QtInfoMsg, "default", QStringLiteral("logging to %1").arg(file.fileName())));

        for (;;) {
            std::unique_lock lock(mutex);
            const auto hasDrops = [&] {
                for (const auto count : dropped) if (count) return true;
                return false;
            };
            ready.wait(lock, [&] { return stopping || !queue.empty() || hasDrops(); });
            if (queue.empty() && !hasDrops() && stopping) break;
            // At most one bounded batch plus one bounded producer queue.
            // No filesystem, sink, stderr or formatting call holds this lock.
            std::deque<QByteArray> batch;
            batch.swap(queue);
            bytes = 0;
            const auto lost = std::exchange(dropped, {});
            lock.unlock();
            for (const auto &line : batch) write(line);
            quint64 total = 0;
            for (const auto count : lost) total += count;
            if (total) {
                write(formatRecord(QtWarningMsg, "logger",
                    QStringLiteral("overload dropped %1 records (debug=%2 info=%3 warning=%4 critical=%5 fatal=%6)")
                        .arg(total).arg(lost[QtDebugMsg]).arg(lost[QtInfoMsg])
                        .arg(lost[QtWarningMsg]).arg(lost[QtCriticalMsg]).arg(lost[QtFatalMsg])));
            }
            if (!options.sink && file.isOpen() && !file.flush()) reportFailure();
        }
        inWriter = false;
    }
};

FileLogger::FileLogger(Options options) : d(std::make_unique<State>(std::move(options)))
{
    d->worker = std::thread([this] { d->run(); });
}

FileLogger::~FileLogger() { shutdown(); }

void FileLogger::shutdown()
{
    {
        std::lock_guard lock(d->mutex);
        d->stopping = true;
    }
    d->ready.notify_one();
    if (d->worker.joinable()) d->worker.join();
}

bool FileLogger::submit(QtMsgType type, const char *category, const QString &message)
{
    const QByteArray line = formatRecord(type, category, message);
    bool accepted = false;
    {
        std::lock_guard lock(d->mutex);
        if (d->stopping) return false;
        if (d->queue.size() >= size_t(d->options.maxRecords)
            || line.size() > d->options.maxBytes - d->bytes) {
            const auto index = unsigned(type) < d->dropped.size() ? unsigned(type) : unsigned(QtInfoMsg);
            ++d->dropped[index];
        } else {
            d->queue.push_back(line);
            d->bytes += line.size();
            accepted = true;
        }
    }
    d->ready.notify_one();
    return accepted;
}

void FileLogger::install()
{
    install(Options{});
}

void FileLogger::install(Options options)
{
    static Installation installation = [options = std::move(options)]() mutable {
        // Initialize every formatter/sanitizer dependency before registering
        // Installation's destructor. Its fallback ::exit path then joins the
        // writer before those function-local statics can be destroyed.
        (void)formatRecord(QtInfoMsg, "default", QStringLiteral("logger initialization"));
        // Startup directory resolution stays here; ongoing writes and all
        // QFile ownership belong to the single writer thread.
        if (options.path.isEmpty()) {
            QString dir = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                          + QStringLiteral("/../logs"));
            if (!QDir().mkpath(dir)) dir = QDir::tempPath();
            options.path = dir + QStringLiteral("/frame.log");
        }
        options.announcePath = true;
        auto logger = std::make_shared<FileLogger>(std::move(options));
        {
            auto &dispatch = installedLogger();
            std::lock_guard lock(dispatch.mutex);
            dispatch.logger = logger;
        }
        qInstallMessageHandler(handler);
        return Installation{std::move(logger)};
    }();
}

void FileLogger::shutdownInstalled()
{
    auto &dispatch = installedLogger();
    std::shared_ptr<FileLogger> logger;
    {
        std::unique_lock lock(dispatch.mutex);
        logger = std::exchange(dispatch.logger, {});
        dispatch.idle.wait(lock, [&] { return dispatch.handlers == 0; });
    }
    if (logger) logger->shutdown();
}
