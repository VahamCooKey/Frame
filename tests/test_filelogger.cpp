#include <QtTest>
#include "core/FileLogger.h"
#include <condition_variable>
#include <mutex>
#include <future>
#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdlib>

class TestFileLogger : public QObject {
    Q_OBJECT
private slots:
    void boundedSlowSinkAndDrain() {
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false, release = false;
        QByteArray output;
        FileLogger::Options options;
        options.maxRecords = 2;
        options.maxBytes = 4096;
        options.sink = [&](const QByteArray &line) {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return release; });
            output += line;
            cv.notify_all();
        };
        FileLogger logger(options);
        QVERIFY(logger.submit(QtInfoMsg, "test", "first"));
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!cv.wait_for(lock, std::chrono::seconds(5), [&] { return entered; })) {
                release = true; cv.notify_all(); QFAIL("worker did not enter sink");
            }
        }
        // Sink stays blocked throughout these synchronous submissions. Completion
        // proves producers do not acquire its lock or wait for its disk operation.
        auto submissions = std::async(std::launch::async, [&] {
            return std::array<bool, 3>{
                logger.submit(QtWarningMsg, "test", "second"),
                logger.submit(QtInfoMsg, "test", "https://example.test/private?token=SYNTHETIC"),
                logger.submit(QtCriticalMsg, "test", "overflow")};
        });
        const bool producersReturned = submissions.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
        { std::lock_guard<std::mutex> lock(mutex); release = true; }
        cv.notify_all();
        const auto accepted = submissions.get();
        bool reported = false;
        {
            std::unique_lock lock(mutex);
            reported = cv.wait_for(lock, std::chrono::seconds(3), [&] { return output.contains("overload dropped 1"); });
        }
        const bool recovered = logger.submit(QtInfoMsg, "test", "after-overload");
        logger.shutdown();
        QVERIFY(producersReturned);
        QVERIFY(accepted[0]); QVERIFY(accepted[1]); QVERIFY(!accepted[2]);
        QVERIFY(reported); QVERIFY(recovered);
        QVERIFY(output.indexOf("first") < output.indexOf("second"));
        QVERIFY(!output.contains("SYNTHETIC"));
        QVERIFY(output.contains("overload dropped 1"));
        QVERIFY(output.contains("critical=1"));
        QVERIFY(output.contains("after-overload"));
        QVERIFY(!logger.submit(QtInfoMsg, "test", "after shutdown"));
    }
    void byteBoundAndOversizedRecord() {
        QByteArray output;
        FileLogger::Options options;
        options.maxBytes = 64;
        options.sink = [&](const QByteArray &line) { output += line; };
        FileLogger logger(options);
        // A single record must fit even when the worker is completely idle.
        QVERIFY(!logger.submit(QtInfoMsg, "test", QString(1024, 'X')));
        logger.shutdown();
        QVERIFY(output.contains("overload dropped 1"));
        QVERIFY(!output.contains(QByteArray(100, 'X')));
    }
    void concurrentProducersKeepPerThreadOrder() {
        QByteArray output;
        FileLogger::Options options;
        options.maxRecords = 2000;
        options.sink = [&](const QByteArray &line) { output += line; };
        FileLogger logger(options);
        std::atomic_bool rejected{false};
        {
            std::vector<std::jthread> producers;
            for (int t = 0; t < 4; ++t)
                producers.emplace_back([&, t] {
                    for (int i = 0; i < 250; ++i)
                        if (!logger.submit(QtInfoMsg, "threads", QString("thread%1-record%2-end").arg(t).arg(i))) rejected = true;
                });
        }
        logger.shutdown();
        QVERIFY(!rejected.load());
        for (int t = 0; t < 4; ++t) {
            qsizetype last = -1;
            for (int i = 0; i < 250; ++i) {
                const auto marker = QString("thread%1-record%2-end").arg(t).arg(i).toUtf8();
                const auto offset = output.indexOf(marker);
                QVERIFY(offset > last);
                QCOMPARE(output.count(marker), 1);
                last = offset;
            }
        }
    }
    void rotates() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        FileLogger::Options options;
        options.path = dir.filePath("frame.log");
        options.rotationBytes = 160;
        {
            FileLogger logger(options);
            for (int i = 0; i < 10; ++i)
                QVERIFY(logger.submit(QtInfoMsg, "rotation", QString("record %1").arg(i)));
        }
        QFile current(options.path), previous(options.path + ".1");
        QVERIFY(current.open(QIODevice::ReadOnly));
        QVERIFY(previous.open(QIODevice::ReadOnly));
        QVERIFY(current.readAll().contains("record 9"));
    }
    void obstructedRotationStaysBounded() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        FileLogger::Options options;
        options.path = dir.filePath("frame.log");
        options.rotationBytes = 160;
        QVERIFY(QDir().mkpath(options.path + ".1/keep"));
        {
            FileLogger logger(options);
            for (int i = 0; i < 100; ++i)
                QVERIFY(logger.submit(QtInfoMsg, "rotation", QString("record %1").arg(i)));
        }
        QFile file(options.path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.size() <= options.rotationBytes);
        QVERIFY(file.readAll().contains("record 0"));
        QVERIFY(QDir(options.path + ".1/keep").exists());
    }
    void installedHandlerLifetime_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("normal-concurrent-close") << QString("normal");
        QTest::newRow("static-exit-drain") << QString("exit");
        QTest::newRow("fatal-handler-fallback") << QString("fatal-handler");
    }
    void installedHandlerLifetime() {
        QFETCH(QString, mode);
        QTemporaryDir dir;
        const auto path = dir.filePath("installed.log");
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {"--installed-child", mode, path});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto output = file.readAll();
        QVERIFY(output.contains("logging to"));
        QVERIFY(output.contains("[redacted]"));
        QVERIFY(!output.contains("SYNTHETIC"));
        const auto diagnostics = process.readAllStandardError();
        QVERIFY(!diagnostics.contains("SYNTHETIC"));
        QVERIFY(diagnostics.contains("after shutdown suppressed"));
        if (mode == "fatal-handler") {
            QVERIFY(diagnostics.contains("FATAL"));
            QVERIFY(diagnostics.contains("[redacted]"));
        }
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 4 && args.at(1) == "--installed-child") {
        struct LateDiagnostic {
            ~LateDiagnostic() { qInfo("late password=SYNTHETIC_LATE"); }
        };
        static LateDiagnostic late; // deliberately destroyed after Installation
        FileLogger::Options options;
        options.path = args.at(3);
        options.maxRecords = 2;
        options.maxBytes = 256;
        FileLogger::install(options);
        qInfo("https://user:SYNTHETIC_PASSWORD@example.test/SYNTHETIC_PATH?credential=SYNTHETIC_QUERY");
        QThread::msleep(30); // ensure the first admitted record reached the file
        if (args.at(2) == "fatal-handler") {
            // Exercise the installed fatal route without deliberately invoking
            // Windows crash dialogs. This is not an actual abort/SEH test.
            const auto callback = qInstallMessageHandler(nullptr);
            qInstallMessageHandler(callback);
            callback(QtFatalMsg, QMessageLogContext(), "password=SYNTHETIC_FATAL");
        } else {
            std::vector<std::jthread> producers;
            for (int t = 0; t < 4; ++t)
                producers.emplace_back([] {
                    for (int i = 0; i < 200; ++i) qInfo("concurrent credential=SYNTHETIC_WORKER");
                });
            if (args.at(2) == "normal") FileLogger::shutdownInstalled();
        }
        if (args.at(2) == "exit") std::exit(0);
        FileLogger::shutdownInstalled();
        return 0;
    }
    TestFileLogger test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_filelogger.moc"
