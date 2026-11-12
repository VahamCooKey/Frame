// CONTRACT v2 — owned by lead. Amend via CONTRACTS.md only.
// v2: amendment A54 — revealInFileManager() default: open the containing
//     folder through QDesktopServices. Windows overrides with Explorer's
//     /select so the screenshot itself is highlighted.
// v1: amendment A27 — cross-platform Phase 1. Extracted from
//     NativeWindowWin32 v7: of INativeWindow's thirteen invokables, eight
//     were already plain Qt (QWindow::startSystemMove/Resize, ::visibility,
//     QGuiApplication::primaryScreen, QClipboard) sitting in the Windows-only
//     file. This class gives every platform that implementation for free;
//     NativeWindowWin32/Linux/Mac inherit it and override only the members
//     that are genuinely native (see notes/cross-platform-plan.md §0.1).
//
// NOT itself a full INativeWindow: setFullscreenWindow(), the three file
// dialogs, and animationsEnabled() stay pure virtual here. Each platform
// subclass must still provide those.
#pragma once
#include "INativeWindow.h"
#include <QPointer>

class NativeWindowBase : public INativeWindow {
public:
    explicit NativeWindowBase(QObject *parent = nullptr) : INativeWindow(parent) {}

    // Concrete platform attach() implementations MUST call
    // NativeWindowBase::attach(window) first — this is what makes
    // startMove()/toggleMaximize()/etc. work at all.
    void attach(QQuickWindow *window) override;

    // Trailing minimize/maximize/close — the Windows and most Linux desktop
    // convention. NativeWindowMac overrides both (leading, different order).
    TitleBarLayout titleBarLayout() const override;
    QVariantList titleBarButtons() const override;
    bool titleBarButtonsLeading() const override;

    // Rule 1 (INativeWindow.h): both delegate to QWindow::startSystem*, never
    // a computed setPosition()/move() — this is what keeps Wayland working.
    void startMove() override;
    void startResize(int edges) override;

    void toggleMaximize() override;
    void minimize() override;
    bool isMaximized() const override;

    bool alwaysOnTop() const override { return m_alwaysOnTop; }
    void setAlwaysOnTop(bool enabled) override;

    QString clipboardText() const override;
    void revealInFileManager(const QString &path) override;
    QVariantMap primaryScreenGeometry() const override;

    // Restore from minimized, raise and request activation through QWindow.
    // NativeWindowWin32 adds the Win32 foreground calls on top.
    void allowForegroundHandoff() override {}
    void raiseToFront() override;

protected:
    virtual void applyAlwaysOnTop();
    QPointer<QQuickWindow> m_window;
    bool m_alwaysOnTop{false};

private:
    QMetaObject::Connection m_stateConnection;
    QMetaObject::Connection m_visibilityConnection;
};
