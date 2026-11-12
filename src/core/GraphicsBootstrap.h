#pragma once

#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QThread>
#include <memory>

// Overlap Windows' initial WGL/driver setup with application/QML setup.
// This does not own Qt Quick's render context or remove its exposure sync.
// Keep this object inside QGuiApplication's lifetime, including early exits.
class GraphicsBootstrap final {
public:
    GraphicsBootstrap()
    {
#if defined(Q_OS_WIN) && defined(QT_OPENGL_DYNAMIC)
        if (qEnvironmentVariableIntValue("FRAME_NO_GRAPHICS_BOOTSTRAP") == 1
            || qgetenv("QT_OPENGL").toLower() == "software")
            return;
        const bool trace = qEnvironmentVariableIsSet("FRAME_PERF");
        m_thread.reset(QThread::create([trace] {
            QElapsedTimer timer;
            timer.start();
            // Qt's Windows dynamic-GL plugin resolves the module through
            // its mutex-protected static initialization. Leave renderer
            // selection, driver workarounds and fallback entirely to Qt.
            // No extra application GL context, screen or surface is needed.
            const auto module = QOpenGLContext::openGLModuleType();
            if (trace)
                qInfo("perf: graphics-bootstrap module=%d ms=%lld", int(module),
                      static_cast<long long>(timer.elapsed()));
        }));
        m_thread->start();
#endif
    }

    ~GraphicsBootstrap()
    {
        // Qt's platform integration must outlive a context creation already
        // in flight, even when CLI parsing or initialization exits early.
        if (m_thread) m_thread->wait();
    }

    GraphicsBootstrap(const GraphicsBootstrap &) = delete;
    GraphicsBootstrap &operator=(const GraphicsBootstrap &) = delete;

private:
    std::unique_ptr<QThread> m_thread;
};
