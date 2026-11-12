// CONTRACT v1 — owned by lead. Amend via CONTRACTS.md only.
// v1: amendment A27 — cross-platform Phase 1 stub.
//
// Deliberately plain C++ for now (no Objective-C++, no AppKit link). The
// traffic-light decision in notes/cross-platform-plan.md §1.3 is still open:
// this class draws our own caption buttons (NativeWindowBase's default
// trailing layout, overridden below to leading/close-minimize-maximize
// order) rather than real NSWindow buttons. Real traffic lights, real
// reduce-motion, and native dialogs all mean converting this to a .mm file
// — tracked in the plan, not attempted here.
//
// UNVERIFIED: written against the Qt API and the plan, not compiled or run
// — this machine has no macOS toolchain.
#pragma once
#include "NativeWindowBase.h"

class NativeWindowMac : public NativeWindowBase {
    Q_OBJECT
public:
    explicit NativeWindowMac(QObject *parent = nullptr);

    // Rule 2 (INativeWindow.h): macOS puts its window controls on the
    // leading edge, close-minimize-zoom order — the one place this class
    // must diverge from NativeWindowBase's Windows/Linux default.
    TitleBarLayout titleBarLayout() const override;
    QVariantList titleBarButtons() const override;
    bool titleBarButtonsLeading() const override;

    void setFullscreenWindow(bool enabled,
                             int restoreX, int restoreY,
                             int restoreWidth, int restoreHeight) override;

    // TODO (plan §4.4): QtQuick.Dialogs FileDialog behind a Deferred*
    // Loader, same as Linux — see NativeWindowLinux.h.
    QString openMediaFileDialog() const override;
    QStringList openMediaFilesDialog() const override;
    QString openShaderFileDialog() const override;
    QString openSubtitleFileDialog() const override;
    QString openFolderDialog() const override;

    // TODO (plan §4.3): NSWorkspace.accessibilityDisplayShouldReduceMotion,
    // which needs Objective-C++. `true` is a safe default in the meantime.
    bool animationsEnabled() const override;
    // No idle inhibit yet: it needs IOPMAssertionCreateWithName.
    void setPlaybackActive(bool playing, bool hasVideo) override;
};
