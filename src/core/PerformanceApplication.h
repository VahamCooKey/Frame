#pragma once

#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QThread>

// Optional inclusive GUI event timing. Copy the class label before delivery:
// an event handler can destroy its receiver. Never record payloads/object names.
class PerformanceApplication final : public QGuiApplication {
public:
    PerformanceApplication(int &argc, char **argv) : QGuiApplication(argc, argv) {}

    bool notify(QObject *receiver, QEvent *event) override
    {
        if (!m_trace || QThread::currentThread() != thread())
            return QGuiApplication::notify(receiver, event);
        const QByteArray type = receiver ? receiver->metaObject()->className() : "null";
        const int eventType = event ? static_cast<int>(event->type()) : -1;
        const int depth = m_depth++;
        QElapsedTimer clock;
        clock.start();
        const bool handled = QGuiApplication::notify(receiver, event);
        const qint64 ns = clock.nsecsElapsed();
        --m_depth;
        if (ns >= 50'000'000)
            qInfo("perf: gui-event type=%d receiver=%s depth=%d ms=%.2f",
                  eventType, type.constData(), depth, ns / 1e6);
        return handled;
    }

private:
    const bool m_trace{qEnvironmentVariableIsSet("FRAME_PERF")};
    int m_depth{0};
};
