#include "NativeWindowBase.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QUrl>

void NativeWindowBase::attach(QQuickWindow *window)
{
    disconnect(m_stateConnection);
    disconnect(m_visibilityConnection);
    m_window = window;
    if (window) {
        m_stateConnection = connect(window, &QWindow::windowStateChanged, this,
                                   [this] { applyAlwaysOnTop(); });
        m_visibilityConnection = connect(window, &QWindow::visibilityChanged, this,
                                        [this] { applyAlwaysOnTop(); });
        applyAlwaysOnTop();
    }
}

void NativeWindowBase::setAlwaysOnTop(bool enabled)
{
    if (m_alwaysOnTop == enabled)
        return;
    m_alwaysOnTop = enabled;
    applyAlwaysOnTop();
    emit alwaysOnTopChanged(enabled);
}

void NativeWindowBase::applyAlwaysOnTop()
{
    if (m_window && m_window->flags().testFlag(Qt::WindowStaysOnTopHint) != m_alwaysOnTop)
        m_window->setFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
}

void NativeWindowBase::raiseToFront()
{
    if (!m_window)
        return;
    if (m_window->windowStates() & Qt::WindowMinimized)
        m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
    m_window->raise();
    m_window->requestActivate();
}

TitleBarLayout NativeWindowBase::titleBarLayout() const
{
    TitleBarLayout layout;
    layout.buttonsLeading = titleBarButtonsLeading();
    layout.buttons = titleBarButtons();
    return layout;
}

QVariantList NativeWindowBase::titleBarButtons() const
{
    const auto makeButton = [](const QString &id, const QString &glyph) {
        QVariantMap button;
        button.insert(QStringLiteral("id"), id);
        button.insert(QStringLiteral("glyph"), glyph);
        return QVariant(button);
    };

    QVariantList buttons;
    buttons.append(makeButton(QStringLiteral("minimize"), QStringLiteral("—")));
    buttons.append(makeButton(QStringLiteral("maximize"), QStringLiteral("□")));
    buttons.append(makeButton(QStringLiteral("close"), QStringLiteral("✕")));
    return buttons;
}

bool NativeWindowBase::titleBarButtonsLeading() const
{
    return false;
}

void NativeWindowBase::startMove()
{
    if (m_window)
        m_window->startSystemMove();
}

void NativeWindowBase::startResize(int edges)
{
    if (m_window)
        m_window->startSystemResize(static_cast<Qt::Edges>(edges));
}

void NativeWindowBase::toggleMaximize()
{
    if (!m_window)
        return;

    if (m_window->visibility() == QWindow::Maximized)
        m_window->setVisibility(QWindow::Windowed);
    else
        m_window->setVisibility(QWindow::Maximized);
}

void NativeWindowBase::minimize()
{
    if (m_window)
        m_window->showMinimized();
}

bool NativeWindowBase::isMaximized() const
{
    return m_window && m_window->visibility() == QWindow::Maximized;
}

QString NativeWindowBase::clipboardText() const
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->text() : QString();
}

QVariantMap NativeWindowBase::primaryScreenGeometry() const
{
    // F37: the attached Screen QML type follows the window, which the window
    // manager may have created on a secondary monitor — the idle-size layout
    // must follow the MAIN display instead.
    QVariantMap geo;
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        geo.insert(QStringLiteral("x"), available.x());
        geo.insert(QStringLiteral("y"), available.y());
        geo.insert(QStringLiteral("width"), available.width());
        geo.insert(QStringLiteral("height"), available.height());
    }
    return geo;
}

void NativeWindowBase::revealInFileManager(const QString &path)
{
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    // The folder is the portable answer; only Windows can also select.
    const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
        qWarning("reveal: could not open '%ls'", qUtf16Printable(folder));
}
