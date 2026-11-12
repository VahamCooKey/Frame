// CONTRACT v4 — owned by lead. Amend via CONTRACTS.md only.
// v4: amendment A54 — revealInFileManager(path): the screenshot toast's
//     "show in folder". NativeWindowBase opens the folder; Windows selects
//     the file in Explorer.
// v3: amendment A36 — openSubtitleFileDialog() and openFolderDialog()
//     added to native interface and cross-platform stubs.
// v2: amendment A26 — the interface is now a QObject and carries the whole
//     QML-facing surface as pure virtuals. Previously it declared four
//     methods while QML called eleven on the concrete Win32 class, so a
//     second platform would have compiled and then failed at runtime on
//     every menu click. Missing one is now a compile error.
//
// Platform seam for frameless-window behavior and system integration.
// `Native` in QML is a pointer to THIS type: everything QML may call is
// declared here, and nothing may be added to a concrete platform class
// without being added here first.
//
// Rule 1: startMove()/startResize() MUST delegate to QWindow::startSystemMove()
// / startSystemResize(). Never compute a delta and call setPosition()/move().
// Wayland clients cannot set their own window position at all.
//
// Rule 2: titleBarButtons()/titleBarButtonsLeading() are DATA. TitleBar.qml
// reads button order and side from them and must never hardcode trailing
// Windows-style controls — macOS puts traffic lights on the leading edge.
#pragma once
#include <QGuiApplication>
#include <QScreen>
#include <QVariantMap>
#include <QObject>
#include <QQuickWindow>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

struct TitleBarLayout {
    bool buttonsLeading{false};   // macOS: true (traffic lights on the left)
    QVariantList buttons;         // [{ id: "minimize"|"maximize"|"close", glyph: QString }]
};

class INativeWindow : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)
public:
    explicit INativeWindow(QObject *parent = nullptr) : QObject(parent) {}
    ~INativeWindow() override = default;

    // Not Q_INVOKABLE: called from main.cpp, never from QML.
    virtual void attach(QQuickWindow *window) = 0;
    virtual TitleBarLayout titleBarLayout() const = 0;

    // --- everything below is called from qml/ ---

    // Rule 1 applies to both.
    Q_INVOKABLE virtual void startMove() = 0;
    Q_INVOKABLE virtual void startResize(int edges) = 0;

    Q_INVOKABLE virtual void toggleMaximize() = 0;
    Q_INVOKABLE virtual void minimize() = 0;
    Q_INVOKABLE virtual bool isMaximized() const = 0;

    // The user's windowed preference survives fullscreen's temporary native
    // topmost placement. Changing it must not recreate a custom native frame.
    virtual bool alwaysOnTop() const = 0;
    Q_INVOKABLE virtual void setAlwaysOnTop(bool enabled) = 0;
    Q_INVOKABLE void toggleAlwaysOnTop() { setAlwaysOnTop(!alwaysOnTop()); }
    // Available geometry {x, y, width, height} of the screen under a
    // global point (the window's centre for the mini player and window
    // size presets); the primary screen when no screen contains it.
    Q_INVOKABLE QVariantMap screenWorkArea(int x, int y) const
    {
        QScreen *screen = QGuiApplication::screenAt(QPoint(x, y));
        if (!screen) screen = QGuiApplication::primaryScreen();
        const QRect r = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
        return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
                {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()}};
    }

    // Rule 2. `buttons` is ordered leading-to-trailing as drawn.
    Q_INVOKABLE virtual QVariantList titleBarButtons() const = 0;
    Q_INVOKABLE virtual bool titleBarButtonsLeading() const = 0;

    // The restore rectangle is passed in because the platform layer must not
    // depend on QML's notion of the pre-fullscreen geometry.
    Q_INVOKABLE virtual void setFullscreenWindow(bool enabled,
                                                 int restoreX, int restoreY,
                                                 int restoreWidth,
                                                 int restoreHeight) = 0;

    // Native file pickers. Empty return means the user cancelled.
    Q_INVOKABLE virtual QString openMediaFileDialog() const = 0;
    // F10: multi-select. Natural (numeric-collated) order, not shell
    // selection order, so a multi-pick queues like the F16 sibling enqueue.
    Q_INVOKABLE virtual QStringList openMediaFilesDialog() const = 0;
    Q_INVOKABLE virtual QString openShaderFileDialog() const = 0;
    Q_INVOKABLE virtual QString openSubtitleFileDialog() const = 0;
    Q_INVOKABLE virtual QString openFolderDialog() const = 0;

    // F45. Plain QClipboard on every platform so far, but it stays on the
    // seam because this is the system-integration surface QML talks to.
    Q_INVOKABLE virtual QString clipboardText() const = 0;

    // A54. Open the system file manager on <path>'s folder, selecting the
    // file where the platform can (Explorer /select). Never blocks: the
    // file manager is launched detached. Missing file: opens the folder.
    Q_INVOKABLE virtual void revealInFileManager(const QString &path) = 0;

    // The OS "reduce animations" preference. QML collapses its animation
    // durations to 0 when this is false, so every platform must answer it
    // (Windows: SPI_GETCLIENTAREAANIMATION; macOS: reduceMotion; GNOME:
    // gtk-enable-animations).
    Q_INVOKABLE virtual bool animationsEnabled() const = 0;

    // F37: available geometry {x, y, width, height} of the PRIMARY screen,
    // regardless of which screen this window was created on.
    Q_INVOKABLE virtual QVariantMap primaryScreenGeometry() const = 0;

    // --- called from main.cpp, never from QML ---

    // Keeps the machine (and, with video, the display) awake while something
    // plays; false releases the hold. libmpv's own stop-screensaver only
    // works through mpv's own window, so the app has to do this itself.
    virtual void setPlaybackActive(bool playing, bool hasVideo) = 0;

    // Second-launch handoff: a new Frame process forwards its file to the
    // running one and exits. The sender calls allowForegroundHandoff()
    // before forwarding (Windows only grants focus changes to the process
    // the user just launched), and the receiver calls raiseToFront().
    virtual void allowForegroundHandoff() = 0;
    virtual void raiseToFront() = 0;

signals:
    void alwaysOnTopChanged(bool enabled);
};
