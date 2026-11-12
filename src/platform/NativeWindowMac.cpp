// macOS implementation of INativeWindow. See the header for the UNVERIFIED
// note — this file has never been built (no Xcode toolchain available here).
#include "NativeWindowMac.h"

#include <QQuickWindow>

NativeWindowMac::NativeWindowMac(QObject *parent)
    : NativeWindowBase(parent)
{
}

TitleBarLayout NativeWindowMac::titleBarLayout() const
{
    TitleBarLayout layout;
    layout.buttonsLeading = true;
    layout.buttons = titleBarButtons();
    return layout;
}

QVariantList NativeWindowMac::titleBarButtons() const
{
    // Traffic-light order: close, minimize, zoom(maximize) — left to right.
    const auto makeButton = [](const QString &id, const QString &glyph) {
        QVariantMap button;
        button.insert(QStringLiteral("id"), id);
        button.insert(QStringLiteral("glyph"), glyph);
        return QVariant(button);
    };

    QVariantList buttons;
    buttons.append(makeButton(QStringLiteral("close"), QStringLiteral("✕")));
    buttons.append(makeButton(QStringLiteral("minimize"), QStringLiteral("—")));
    buttons.append(makeButton(QStringLiteral("maximize"), QStringLiteral("□")));
    return buttons;
}

bool NativeWindowMac::titleBarButtonsLeading() const
{
    return true;
}

void NativeWindowMac::setFullscreenWindow(bool enabled,
                                          int restoreX, int restoreY,
                                          int restoreWidth, int restoreHeight)
{
    if (!m_window)
        return;

    if (enabled) {
        m_window->showFullScreen();
    } else {
        m_window->showNormal();
        m_window->setGeometry(restoreX, restoreY, restoreWidth, restoreHeight);
    }
}

QString NativeWindowMac::openMediaFileDialog() const
{
    return {};
}

QStringList NativeWindowMac::openMediaFilesDialog() const
{
    return {};
}

QString NativeWindowMac::openShaderFileDialog() const
{
    return {};
}

QString NativeWindowMac::openSubtitleFileDialog() const
{
    return {};
}

QString NativeWindowMac::openFolderDialog() const
{
    return {};
}

bool NativeWindowMac::animationsEnabled() const
{
    return true;
}

void NativeWindowMac::setPlaybackActive(bool playing, bool hasVideo)
{
    // Not implemented: needs an IOPMAssertion (kIOPMAssertionTypeNoDisplaySleep).
    Q_UNUSED(playing);
    Q_UNUSED(hasVideo);
}
