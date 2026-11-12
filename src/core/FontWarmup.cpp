#include "FontWarmup.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QThread>

void startFontDatabaseWarmup()
{
    // Diagnostic kill switch for explicitly opted-in A/B timing runs.
    if (!qEnvironmentVariableIsEmpty("FRAME_NO_FONT_WARMUP"))
        return;
    static bool started = false;
    if (started)
        return;
    started = true;
    QThread *worker = QThread::create([] {
        QElapsedTimer clock;
        clock.start();
        // families() fills in the family names; styles() populates one
        // family's faces. Each call takes and releases the database lock,
        // so the GUI thread can get in between families.
        const QStringList families = QFontDatabase::families();
        qInfo("fonts: warming %lld families", static_cast<long long>(families.size()));
        qint64 slowestMs = 0;
        QString slowest;
        for (const QString &family : families) {
            if (QThread::currentThread()->isInterruptionRequested())
                return;
            const qint64 before = clock.elapsed();
            QFontDatabase::styles(family);
            const qint64 took = clock.elapsed() - before;
            if (took > slowestMs) {
                slowestMs = took;
                slowest = family;
            }
            // Give the GUI thread a real gap between families. Without it
            // the worker re-takes the database lock immediately and a GUI
            // thread that needs it (a new label, a tooltip, the OSD coming
            // up on pause) can wait through many families in a row.
            QThread::msleep(1);
        }
        qInfo("fonts: database warmed, %lld families in %lld ms (slowest '%ls' %lld ms)",
              static_cast<long long>(families.size()), static_cast<long long>(clock.elapsed()),
              qUtf16Printable(slowest), static_cast<long long>(slowestMs));
    });
    worker->setObjectName(QStringLiteral("font-warmup"));
    // Finish before QGuiApplication tears down the font database. A quick
    // close can otherwise leave the worker using destroyed platform data.
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                     worker, [worker] {
        worker->requestInterruption();
        worker->wait();
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    // Normal priority on purpose. The worker holds Qt's font database lock
    // while it populates one family; at LowestPriority a busy machine (an
    // export, shader compiles) could park it mid-family while the GUI
    // thread sat on that lock -- a priority inversion that showed up as a
    // "Not Responding" window with the audio still running. A61 keeps
    // this scan opt-in: even at NormalPriority, one family can hold the
    // shared database lock for hundreds of milliseconds.
    worker->start(QThread::NormalPriority);
}
