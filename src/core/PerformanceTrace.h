#pragma once

#include "PlayerController.h"
#include <QElapsedTimer>
#include <QMutex>
#include <QQuickWindow>
#include <QTimer>
#include <algorithm>
#include <memory>

// Opt-in diagnostics only (FRAME_PERF=1). Normal playback has no polling
// timer or per-frame sampling cost. These are Qt presentation intervals,
// not a substitute for mpv's decoded/dropped-frame counters.
inline void installPerformanceTrace(QQuickWindow *window, PlayerController *player)
{
    if (!window || !qEnvironmentVariableIsSet("FRAME_PERF"))
        return;

    struct Samples {
        QMutex mutex;
        QElapsedTimer clock;
        QList<double> frameGaps;
        qint64 lastFrameNs{0};
        bool playing{false};
    };
    const auto samples = std::make_shared<Samples>();
    samples->clock.start();
    // Each pair runs on the same scene-graph thread. Capture thread-local
    // phase clocks rather than reading render state from the GUI thread.
    const auto syncClock = std::make_shared<QElapsedTimer>();
    QObject::connect(window, &QQuickWindow::beforeSynchronizing, window,
                     [syncClock] { syncClock->start(); }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::afterSynchronizing, window, [syncClock] {
        if (syncClock->isValid() && syncClock->nsecsElapsed() >= 50'000'000)
            qInfo("perf: scene-sync ms=%.2f", syncClock->nsecsElapsed() / 1e6);
    }, Qt::DirectConnection);
    const auto renderClock = std::make_shared<QElapsedTimer>();
    QObject::connect(window, &QQuickWindow::beforeRendering, window,
                     [renderClock] { renderClock->start(); }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::afterRendering, window, [renderClock] {
        if (renderClock->isValid() && renderClock->nsecsElapsed() >= 50'000'000)
            qInfo("perf: scene-render ms=%.2f", renderClock->nsecsElapsed() / 1e6);
    }, Qt::DirectConnection);
    const auto updateState = [samples, player] {
        const QMutexLocker lock(&samples->mutex);
        const bool playing = player->state() == PlayerController::State::Playing;
        if (samples->playing != playing)
            samples->lastFrameNs = 0;
        samples->playing = playing;
    };
    QObject::connect(player, &PlayerController::stateChanged, window, updateState);
    updateState();
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [samples] {
        const qint64 now = samples->clock.nsecsElapsed();
        const QMutexLocker lock(&samples->mutex);
        if (samples->playing && samples->lastFrameNs && samples->frameGaps.size() < 4096)
            samples->frameGaps.append((now - samples->lastFrameNs) / 1000000.0);
        samples->lastFrameNs = samples->playing ? now : 0;
    }, Qt::DirectConnection);

    auto *heartbeat = new QTimer(window);
    heartbeat->setTimerType(Qt::PreciseTimer);
    heartbeat->setInterval(16);
    QObject::connect(heartbeat, &QTimer::timeout, window,
                     [samples, last = qint64(0), report = qint64(0), maxGui = 0.0]() mutable {
        const qint64 now = samples->clock.nsecsElapsed();
        if (last) {
            maxGui = qMax(maxGui, (now - last) / 1000000.0);
            if (now - last >= 100'000'000)
                qInfo("perf: gui-gap at-ms=%.2f gap-ms=%.2f", now / 1e6, (now - last) / 1e6);
        }
        last = now;
        if (now - report < 5000000000LL)
            return;
        QList<double> gaps;
        {
            const QMutexLocker lock(&samples->mutex);
            gaps.swap(samples->frameGaps);
        }
        std::sort(gaps.begin(), gaps.end());
        const double p95 = gaps.isEmpty() ? 0 : gaps.at((gaps.size() - 1) * 95 / 100);
        qInfo("perf: presented=%lld gap-p95=%.1fms gap-max=%.1fms gui-max=%.1fms",
              static_cast<long long>(gaps.size()), p95, gaps.isEmpty() ? 0 : gaps.last(), maxGui);
        report = now;
        maxGui = 0;
    });
    heartbeat->start();
}
