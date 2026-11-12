// Linux (X11 + Wayland) implementation of INativeWindow. Built and verified
// on WSL Ubuntu 26.04 (A28); Phase 3 gaps tracked in the header.
#include "NativeWindowLinux.h"
#include "LinuxPlaybackInhibitor.h"

#include <QProcess>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>

NativeWindowLinux::NativeWindowLinux(QObject *parent)
    : NativeWindowBase(parent), m_inhibitor(std::make_unique<LinuxPlaybackInhibitor>())
{
    // KDE stores the global animation-speed factor in kdeglobals; 0 means
    // "instant". A missing key or missing file leaves the default `true`.
    const QSettings kde(
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + "/kdeglobals",
        QSettings::IniFormat);
    const QVariant factor = kde.value("KDE/AnimationDurationFactor");
    if (factor.isValid()) {
        m_animationsEnabled = factor.toBool();
        return;
    }

    // GNOME family answers gsettings. One synchronous probe while the
    // constructor runs — before QML loads — so first-frame bindings see the
    // final value. Missing desktop or missing binary keeps the default.
    // ponytail: a ~50 ms gsettings exec on the GNOME startup path; upgrade
    // to an async portal/D-Bus read + notify signal if that ever shows up
    // in the startup log.
    const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP");
    if (!desktop.contains("GNOME") && !desktop.contains("Cinnamon"))
        return;
    if (QStandardPaths::findExecutable("gsettings").isEmpty())
        return;
    QProcess probe;
    probe.start("gsettings",
                {"get", "org.gnome.desktop.interface", "enable-animations"});
    if (!probe.waitForFinished(500))
        return;
    if (probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0)
        m_animationsEnabled = probe.readAllStandardOutput().trimmed() != "false";
}

NativeWindowLinux::~NativeWindowLinux() = default;

void NativeWindowLinux::setFullscreenWindow(bool enabled,
                                            int restoreX, int restoreY,
                                            int restoreWidth, int restoreHeight)
{
    if (!m_window)
        return;

    if (enabled) {
        if (!m_fullscreen)
            m_beforeFullscreen = m_window->windowStates() & ~(Qt::WindowFullScreen | Qt::WindowMinimized);
        m_fullscreen = true;
        m_window->showFullScreen();
    } else {
        m_fullscreen = false;
        if (m_beforeFullscreen.testFlag(Qt::WindowMaximized)) {
            m_window->showMaximized();
            return;
        }
        m_window->showNormal();
        // Best-effort on Wayland: a compositor may decline to place the
        // window at an exact position (plan §5.1). X11 honors this.
        m_window->setGeometry(restoreX, restoreY, restoreWidth, restoreHeight);
    }
}

QString NativeWindowLinux::openMediaFileDialog() const
{
    return {};
}

QStringList NativeWindowLinux::openMediaFilesDialog() const
{
    return {};
}

QString NativeWindowLinux::openShaderFileDialog() const
{
    return {};
}

QString NativeWindowLinux::openSubtitleFileDialog() const
{
    return {};
}

QString NativeWindowLinux::openFolderDialog() const
{
    return {};
}

bool NativeWindowLinux::animationsEnabled() const
{
    // Cached at construction (KDE kdeglobals / GNOME gsettings probe).
    return m_animationsEnabled;
}

void NativeWindowLinux::setPlaybackActive(bool playing, bool hasVideo)
{
    m_inhibitor->setPlaybackActive(playing, hasVideo);
}
