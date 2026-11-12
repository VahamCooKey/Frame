#include "core/GuiStallMonitor.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <utility>

struct GuiStallMonitor::Impl {
    QTimer heartbeat;
    QElapsedTimer clock;
    qint64 lastBeatMs = 0;
    int thresholdMs = 1000;
};

GuiStallMonitor::GuiStallMonitor(int thresholdMs, QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    if (!qEnvironmentVariableIsEmpty("FRAME_NO_STALL_MONITOR")) return;
    bool ok = false;
    const int override = qEnvironmentVariableIntValue("FRAME_STALL_MS", &ok);
    m_impl->thresholdMs = ok && override > 0 ? override : qMax(1, thresholdMs);
    m_impl->clock.start();
    m_impl->heartbeat.setInterval(100);
    m_impl->heartbeat.setTimerType(Qt::CoarseTimer);
    connect(&m_impl->heartbeat, &QTimer::timeout, this, [this] {
        const qint64 now = m_impl->clock.elapsed();
        const qint64 gap = now - std::exchange(m_impl->lastBeatMs, now);
        if (gap >= m_impl->thresholdMs)
            qWarning("gui: recovered heartbeat gap of %lld ms", static_cast<long long>(gap));
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, [this] { m_impl->heartbeat.stop(); });
    m_impl->heartbeat.start();
}

GuiStallMonitor::~GuiStallMonitor() = default;
