// Detects recovered heartbeat gaps on the constructing GUI thread using a
// coarse 100 ms timer. Reports only after the event loop resumes; permanently
// hung stacks require an external debugger or dump collector. No thread is
// suspended and no stack is walked. Construct/use/destroy on the GUI thread.
// FRAME_STALL_MS=<positive milliseconds> overrides the threshold.
// A nonempty FRAME_NO_STALL_MONITOR disables monitoring.
#pragma once

#include <QObject>

#include <memory>

class GuiStallMonitor : public QObject {
    Q_OBJECT
public:
    explicit GuiStallMonitor(int thresholdMs = 1000, QObject *parent = nullptr);
    ~GuiStallMonitor() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
