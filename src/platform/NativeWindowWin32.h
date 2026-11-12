// CONTRACT v8 — owned by lead. Amend via CONTRACTS.md only.
// v8: amendment A27 — cross-platform Phase 1. Base class changed from
//     INativeWindow to NativeWindowBase, which now supplies startMove(),
//     startResize(), toggleMaximize(), minimize(), isMaximized(),
//     titleBarButtons()/…Leading()/titleBarLayout(), clipboardText(),
//     primaryScreenGeometry() and the `m_window` field. This header only
//     declares the genuinely Win32 members: attach() (COM/DWM/subclass),
//     the two file dialogs, animationsEnabled(), and setFullscreenWindow().
//     beginDrag()/beginResize() and defaultTitleBarLayout() are gone — the
//     WndProc calls the inherited startMove()/startResize() directly.
// v7: amendment A26 — the QML surface moved up into INativeWindow, which is
//     now the QObject base. This class declares the same members as
//     `override`; it gained titleBarButtonsLeading() (false on Windows:
//     caption buttons are trailing) and no longer inherits QObject directly.
// v6: amendment A20 — clipboardText() (F45 open-from-clipboard).
// v5: amendment A18 — openMediaFilesDialog() (F10 multi-select open). The
//     single-path openMediaFileDialog() stays, now a first-of wrapper.
// v4: added primaryScreenGeometry() (F37 — the QML idle-size layout must
//     follow the MAIN monitor on multi-monitor setups, not whichever screen
//     the window happened to be created on).
// v3: native border resizing — the attached window's WndProc is subclassed
//     (staticWndProc + subclass state); no public API change.
// v2: added animationsEnabled() (OS animation-effects setting for QML
//     animation durations, amendment 2026-08-31).
//     Private implementation state only since: 1px edge-cover overlay HWND
//     for fullscreen (keeps the swapchain 1px short of the monitor while
//     hiding the seam; no public API change).
#pragma once
#include "NativeWindowBase.h"
#include <QString>
#include <QVariantList>

struct HWND__;

class NativeWindowWin32 : public NativeWindowBase {
    Q_OBJECT
public:
    explicit NativeWindowWin32(QObject *parent = nullptr);
    ~NativeWindowWin32() override;

    void attach(QQuickWindow *window) override;

    void setFullscreenWindow(bool enabled,
                             int restoreX, int restoreY,
                             int restoreWidth, int restoreHeight) override;
    QString openMediaFileDialog() const override;
    // F10: multi-select open. Returns the picked files in natural
    // (QCollator numeric) order, not shell selection order, so a multi-pick
    // queues the way the F16 sibling enqueue does. Empty when cancelled.
    QStringList openMediaFilesDialog() const override;
    QString openShaderFileDialog() const override;
    QString openSubtitleFileDialog() const override;
    QString openFolderDialog() const override;

    // Reflects Windows "Animation effects" (Settings > Accessibility >
    // Visual effects). QML drives its UI animation durations from this;
    // when off, durations collapse to 0 (instant).
    bool animationsEnabled() const override;
    void revealInFileManager(const QString &path) override;
    void setPlaybackActive(bool playing, bool hasVideo) override;
    void allowForegroundHandoff() override;
    void raiseToFront() override;

private:
    void applyAlwaysOnTop() override;

    // Subclassed window procedure: over an 8px physical border zone it
    // answers WM_NCHITTEST with the sizing edge/corner codes, sets the
    // matching sizing cursor on WM_SETCURSOR, and turns WM_NCLBUTTONDOWN
    // into the inherited startResize() (HT* codes alone are cosmetic on
    // frameless windows). Never claims a zone while fullscreen or
    // maximized. Installed in attach(), restored in the destructor. One
    // subclassed window per process (the QML main window).
    //
    // The raw fixed-width signatures mirror the Win32 WNDPROC/LRESULT types
    // (x64: WPARAM = unsigned long long, LPARAM/LRESULT = long long) so
    // this header stays free of windows.h.
    static long long staticWndProc(HWND__ *hwnd, unsigned int message,
                                   unsigned long long wParam, long long lParam);
    using SubclassedProc = long long (*)(HWND__ *, unsigned int,
                                         unsigned long long, long long);
    static NativeWindowWin32 *s_instance;
    static HWND__ *s_subclassedHwnd;
    static SubclassedProc s_originalProc;

    // Plain Win32 1px-tall topmost window covering the row the main window
    // leaves uncovered in fullscreen (NVIDIA full-screen classification
    // guard). Created/shown on enter, hidden on exit, owned by this object.
    HWND__ *m_edgeCover{nullptr};
    // Set for the duration of the fullscreen (1px-extended) geometry; blocks
    // WM_NCHITTEST resize zones so the off-screen 1px row never becomes a
    // resize grab.
    bool m_fullscreen{false};
};
