// CONTRACT v2 — owned by lead. Amend via CONTRACTS.md only.
// v1: amendment A27 — cross-platform Phase 1 stub. X11 and Wayland share
//     this class at the seam level: both get their window behavior from
//     NativeWindowBase's QWindow::startSystemMove/Resize calls, which is
//     the ONE thing that must never regress into a computed setPosition()
//     (Wayland forbids a client positioning its own window at all — see
//     INativeWindow.h Rule 1 and notes/cross-platform-plan.md §5.1).
//
// v2: amendment A29 — Phase 3 (Linux): animationsEnabled() reads the KDE
//     kdeglobals factor or probes GNOME's gsettings once in the constructor
//     (see the .cpp). The three file dialogs stay stubs ON PURPOSE: QML
//     calls them only when Qt.platform.os === "windows"; everywhere else it
//     opens qml/DeferredFileDialog.qml (QtQuick.Dialogs behind a Loader),
//     the same lazy pattern as the other five deferred dialogs. The C++
//     methods remain so the seam's QML-facing surface never changes.
#pragma once
#include "NativeWindowBase.h"
#include <memory>

class LinuxPlaybackInhibitor;

class NativeWindowLinux : public NativeWindowBase {
    Q_OBJECT
public:
    explicit NativeWindowLinux(QObject *parent = nullptr);
    ~NativeWindowLinux() override;

    // Simple compositor-driven fullscreen — no NVIDIA/DWM workaround needed
    // here (plan §4.2: the Win32 1px-overlay trick is Windows+NVIDIA-only).
    void setFullscreenWindow(bool enabled,
                             int restoreX, int restoreY,
                             int restoreWidth, int restoreHeight) override;

    // Stubs by design (v2 note above): QML never calls these off-Windows.
    QString openMediaFileDialog() const override;
    QStringList openMediaFilesDialog() const override;
    QString openShaderFileDialog() const override;
    QString openSubtitleFileDialog() const override;
    QString openFolderDialog() const override;

    // Cached at construction; see the .cpp constructor.
    bool animationsEnabled() const override;
    void setPlaybackActive(bool playing, bool hasVideo) override;

private:
    bool m_animationsEnabled{true};
    std::unique_ptr<LinuxPlaybackInhibitor> m_inhibitor;
    Qt::WindowStates m_beforeFullscreen{Qt::WindowNoState};
    bool m_fullscreen{false};
};
