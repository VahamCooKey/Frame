#pragma once
#include <QByteArray>
#include <QString>
#include <QtLogging>
#include <functional>
#include <memory>

class FileLogger {
public:
    struct Options {
        QString path;
        qsizetype maxRecords = 1024;
        qsizetype maxBytes = 1024 * 1024;
        qint64 rotationBytes = 5 * 1024 * 1024;
        // Used by install(); report the actual opened path, including fallback.
        bool announcePath = false;
        // Runs only on the worker; test sinks may deliberately block.
        std::function<void(const QByteArray &)> sink;
    };
    explicit FileLogger(Options options);
    ~FileLogger();
    bool submit(QtMsgType type, const char *category, const QString &message);
    // Stop accepting records and drain the worker. Like a file close, this
    // can wait for storage; call on the owner, never from a sink callback.
    void shutdown();
    static void install();
    static void install(Options options);
    static void shutdownInstalled();
private:
    struct State;
    std::unique_ptr<State> d;
};
