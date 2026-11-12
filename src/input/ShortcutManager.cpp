#include "input/ShortcutManager.h"
#include "core/PlayerController.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>
#include <QKeySequence>

namespace {

QString specialKeyName(int key)
{
    switch (key) {
    case Qt::Key_Space:                return QStringLiteral("SPACE");
    case Qt::Key_Return:
    case Qt::Key_Enter:                return QStringLiteral("ENTER");
    case Qt::Key_Escape:               return QStringLiteral("ESC");
    case Qt::Key_Tab:                  return QStringLiteral("TAB");
    case Qt::Key_Backspace:            return QStringLiteral("BS");
    case Qt::Key_Delete:               return QStringLiteral("DEL");
    case Qt::Key_Insert:               return QStringLiteral("INS");
    case Qt::Key_Home:                 return QStringLiteral("HOME");
    case Qt::Key_End:                  return QStringLiteral("END");
    case Qt::Key_PageUp:               return QStringLiteral("PGUP");
    case Qt::Key_PageDown:             return QStringLiteral("PGDWN");
    case Qt::Key_Left:                 return QStringLiteral("LEFT");
    case Qt::Key_Right:                return QStringLiteral("RIGHT");
    case Qt::Key_Up:                   return QStringLiteral("UP");
    case Qt::Key_Down:                 return QStringLiteral("DOWN");
    case Qt::Key_MediaPlay:            return QStringLiteral("PLAY");
    case Qt::Key_MediaTogglePlayPause: return QStringLiteral("PLAYPAUSE");
    case Qt::Key_MediaPause:           return QStringLiteral("PAUSE");
    case Qt::Key_MediaStop:            return QStringLiteral("STOP");
    case Qt::Key_MediaPrevious:        return QStringLiteral("PREV");
    case Qt::Key_MediaNext:            return QStringLiteral("NEXT");
    default:                           return QString();
    }
}

// mpv has no "keyunbind": releasing a key is binding it to this.
QString ignoreCommand()
{
    return QStringLiteral("ignore");
}

bool someCommandClaims(const QVariantMap &overrides, const QString &key)
{
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
        if (it.value().toString() == key)
            return true;
    }
    return false;
}

} // namespace

QString ShortcutManager::toMpvKeyName(int key, Qt::KeyboardModifiers mods)
{
    if (key == Qt::Key_unknown)
        return {};

    // Bare modifier presses (Win/Ctrl/Alt/Shift alone) must not become
    // garbage like "Meta" or "Meta+Meta" sent to mpv as keypress.
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
        return {};
    default:
        break;
    }

    // F31: Windows delivers shifted punctuation as the shifted key plus
    // ShiftModifier (e.g. Qt::Key_Greater + Shift for ">"), but mpv's
    // bindings and keypress naming use the bare character. Strip the Shift
    // prefix for these or the ">" / "<" playlist bindings never fire.
    if (key == Qt::Key_Greater || key == Qt::Key_Less) {
        QString name;
        if (mods & Qt::ControlModifier)
            name += QStringLiteral("Ctrl+");
        if (mods & Qt::AltModifier)
            name += QStringLiteral("Alt+");
        if (mods & Qt::MetaModifier)
            name += QStringLiteral("Meta+");
        name += (key == Qt::Key_Greater) ? QStringLiteral(">") : QStringLiteral("<");
        return name;
    }

    QString base = specialKeyName(key);

    if (base.isEmpty() && key >= Qt::Key_F1 && key <= Qt::Key_F12)
        base = QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);

    if (base.isEmpty()) {
        base = QKeySequence(key).toString(QKeySequence::PortableText);
        if (base.isEmpty())
            return {};
        if (base.size() == 1)
            base = base.toLower();
    }

    QString name;
    if (mods & Qt::ControlModifier)
        name += QStringLiteral("Ctrl+");
    if (mods & Qt::AltModifier)
        name += QStringLiteral("Alt+");
    if (mods & Qt::ShiftModifier)
        name += QStringLiteral("Shift+");
    if (mods & Qt::MetaModifier)
        name += QStringLiteral("Meta+");
    name += base;
    return name;
}

ShortcutManager::ShortcutManager(QObject *parent)
    : QObject(parent)
{
    // A14: the hold timer separates an L "tap" from an L "hold" — only when
    // the key is still down when it fires does the hold-shuttle engage.
    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(220);
    connect(m_holdTimer, &QTimer::timeout, this, [this]() {
        if (m_lPressed && !m_holdActive) {
            m_holdActive = true;
            emit shuttleHoldStart(m_lTaps);
        }
    });
}

void ShortcutManager::setPlayer(PlayerController *player)
{
    m_player = player;
    if (!m_player || m_overrides.isEmpty())
        return;

    // Settings are restored before mpv exists, so a stored map is held and
    // issued here. Nothing has reached this player yet, hence the cleared
    // "previous" map: applyOverrides would otherwise undo bindings that were
    // never made.
    const QVariantMap pending = m_overrides;
    m_overrides.clear();
    applyOverrides(pending);
}

void ShortcutManager::setDefaultBindings(const QVariantList &rows)
{
    m_defaultKeyFor.clear();
    m_defaultCommandFor.clear();
    m_defaultKeysFor.clear();

    for (const QVariant &row : rows) {
        const QVariantMap entry = row.toMap();
        const QString key = entry.value(QStringLiteral("key")).toString();
        const QString command = entry.value(QStringLiteral("command")).toString();
        if (key.isEmpty() || command.isEmpty())
            continue;

        m_defaultCommandFor.insert(key, command);
        // A command reachable from several keys (an action and its shifted
        // twin) shows the first one input.conf lists as its shortcut, but
        // every one of them has to be released when it is rebound or
        // disabled — otherwise the twin keeps firing it.
        if (!m_defaultKeyFor.contains(command))
            m_defaultKeyFor.insert(command, key);
        m_defaultKeysFor[command].append(key);
    }
    emit overridesChanged();
}

void ShortcutManager::applyOverrides(const QVariantMap &overrides)
{
    // Undo the set already applied before applying the new one: consecutive
    // calls must not accumulate, or rebinding an action twice leaves the
    // intermediate key still firing it.
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it) {
        const QString defaultKey = m_defaultKeyFor.value(it.key());
        for (const QString &key : m_defaultKeysFor.value(it.key()))
            bind(key, it.key());

        const QString oldKey = it.value().toString();
        if (oldKey.isEmpty() || oldKey == defaultKey)
            continue;
        // The key being released may be some other action's default; hand it
        // back to its owner instead of leaving it dead.
        const QString owner = m_defaultCommandFor.value(oldKey);
        bind(oldKey, owner.isEmpty() ? ignoreCommand() : owner);
    }

    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
        const QString command = it.key();
        const QString newKey = it.value().toString();

        // Every default key of the command is released, not just the one the
        // UI displays: "Play/Pause" sits on SPACE and PLAYPAUSE, and leaving
        // the media key live would make a disabled action still fire.
        const QStringList defaultKeys = m_defaultKeysFor.value(command);

        if (newKey.isEmpty()) {
            for (const QString &key : defaultKeys)
                bind(key, ignoreCommand()); // an empty key means disabled
            continue;
        }

        bind(newKey, command);
        for (const QString &key : defaultKeys) {
            // Unless the key is the new one, or another command in this same
            // map has taken it over — that binding would be clobbered.
            if (key != newKey && !someCommandClaims(overrides, key))
                bind(key, ignoreCommand());
        }
    }

    m_overrides = overrides;
    emit overridesChanged();
}

QString ShortcutManager::commandForKey(const QString &key) const
{
    if (key.isEmpty())
        return {};

    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it) {
        if (it.value().toString() == key)
            return it.key();
    }

    // A default whose command has been rebound (or disabled) elsewhere no
    // longer holds the key, so the conflict check must not report it.
    const QString byDefault = m_defaultCommandFor.value(key);
    if (m_overrides.contains(byDefault))
        return {};
    return byDefault;
}

void ShortcutManager::bind(const QString &key, const QString &command)
{
    if (!m_player || key.isEmpty() || command.isEmpty())
        return;
    m_player->command({QStringLiteral("keybind"), key, command});
}

QString ShortcutManager::keyForCommand(const QString &command) const
{
    if (m_overrides.contains(command)) return m_overrides.value(command).toString();
    for (const QString &key : m_defaultKeysFor.value(command)) {
        if (commandForKey(key) == command) return key;
    }
    return {};
}

QVariantMap ShortcutManager::commandKeys() const
{
    QVariantMap keys;
    for (auto it = m_defaultKeysFor.cbegin(); it != m_defaultKeysFor.cend(); ++it)
        keys.insert(it.key(), keyForCommand(it.key()));
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        keys.insert(it.key(), it.value());
    return keys;
}

void ShortcutManager::installFilter(QWindow *window)
{
    if (window)
        window->installEventFilter(this);
}

void ShortcutManager::setMenuOpen(bool open)
{
    if (m_menuOpen == open)
        return;
    m_menuOpen = open;
    emit menuOpenChanged(m_menuOpen);
}

void ShortcutManager::setWheelOverScroller(bool over)
{
    if (m_wheelOverScroller == over)
        return;
    m_wheelOverScroller = over;
    emit wheelOverScrollerChanged(m_wheelOverScroller);
}

void ShortcutManager::setTextInputActive(bool active)
{
    if (m_textInputActive == active)
        return;
    m_textInputActive = active;
    emit textInputActiveChanged(m_textInputActive);
}

bool ShortcutManager::eventFilter(QObject *obj, QEvent *event)
{
    const auto *window = qobject_cast<QWindow *>(obj);
    const QObject *focus = window ? window->focusObject() : QGuiApplication::focusObject();
    // Text focus is authoritative even if an unrelated closing/preloaded
    // panel just cleared the legacy shared flag.
    const bool editableFocus = focus && focus->property("cursorPosition").isValid()
        && !focus->property("readOnly").toBool();
    // A19: a QML text field owns the keyboard. Forward nothing — not even
    // ESC, which the field handles itself — and let Qt deliver the key to
    // the focused item. Without this, typing a time into the go-to dialog
    // also runs the bindings behind it ("0" seeks to the start).
    if ((m_textInputActive || editableFocus)
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease
            || event->type() == QEvent::Wheel)) {
        return false;
    }
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        // Tab reaches the QML focus chain. A focused player control handles
        // its own activation/navigation; forwarding Space as well would
        // toggle playback a second time behind the button.
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab)
            return false;
        if (focus && focus->property("playlistControl").toBool()) {
            const int key = keyEvent->key();
            const auto modifiers = keyEvent->modifiers();
            const bool arrow = key == Qt::Key_Left || key == Qt::Key_Right
                || key == Qt::Key_Up || key == Qt::Key_Down;
            if ((arrow && (modifiers == Qt::NoModifier || modifiers == Qt::ControlModifier))
                || (key == Qt::Key_F && modifiers == Qt::ControlModifier)
                || (key == Qt::Key_Delete && modifiers == Qt::NoModifier)
                || (key == Qt::Key_Menu && modifiers == Qt::NoModifier)
                || (key == Qt::Key_F10 && modifiers == Qt::ShiftModifier))
                return false;
        }
        if (focus && focus->property("playerControl").toBool()
            && keyEvent->modifiers() == Qt::NoModifier) {
            switch (keyEvent->key()) {
            case Qt::Key_Space:
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Home:
            case Qt::Key_End:
                return false;
            default:
                break;
            }
        }
        if (m_menuOpen && keyEvent->key() != Qt::Key_Escape) {
            // A popup layer owns the keyboard. Menu-navigation keys stay
            // consumed by the popup (forwarding would double-feed them:
            // menu move + mpv seek). F27 (A13): any OTHER shortcut dismisses
            // the picker layer first — QML closes the track menus in the
            // signal handler, which clears menuOpen — and the key then
            // forwards normally, so F/C/etc. work with a menu open.
            switch (keyEvent->key()) {
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                return false;
            default:
                break;
            }
            emit dismissMenusRequested();
            if (m_menuOpen) {
                // A layer QML could not dismiss (e.g. the context menu) still
                // owns the keyboard.
                return false;
            }
        }
        // A15 (F36): hold-shuttle on plain L. A tap released inside the hold
        // window steps the speed up 0.25x (shuttleStepUp); holding past the
        // window engages the hold at the accumulated tap target. L is never
        // forwarded to mpv. (The A12 K+L slow shuttle is gone — J covers
        // slowing down with its own 0.25x steps.)
        if (keyEvent->key() == Qt::Key_L && keyEvent->modifiers() == Qt::NoModifier) {
            if (keyEvent->isAutoRepeat())
                return false;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            m_lTaps = (m_lTaps > 0 && now - m_lastLReleaseMs <= 400)
                          ? qMin(m_lTaps + 1, 3)
                          : 1;
            m_lPressed = true;
            m_holdTimer->start();
            return false;
        }
        handleKeyPress(keyEvent->key(), keyEvent->modifiers());
    } else if (event->type() == QEvent::KeyRelease) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        // A14: releasing L ends an engaged hold (QML reverts speed); a quick
        // release is a speed step. Taps stay accumulated for the 400 ms
        // window so double/triple-click holds work.
        if (keyEvent->key() == Qt::Key_L && keyEvent->modifiers() == Qt::NoModifier
            && !keyEvent->isAutoRepeat()) {
            m_lPressed = false;
            m_lastLReleaseMs = QDateTime::currentMSecsSinceEpoch();
            m_holdTimer->stop();
            if (m_holdActive) {
                m_holdActive = false;
                m_lTaps = 0;
                emit shuttleHoldEnd();
            } else {
                emit shuttleStepUp();
            }
        }
    } else if (event->type() == QEvent::Wheel) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        if (m_menuOpen || m_wheelOverScroller)
            return false;
        handleWheel(wheelEvent->angleDelta().y(), wheelEvent->modifiers());
    }
    return false;
}

bool ShortcutManager::handleKeyPress(int key, Qt::KeyboardModifiers mods)
{
    const QString name = toMpvKeyName(key, mods);
    if (name.isEmpty() || !m_player)
        return false;
    // Resolve the user's binding, not a hard-coded Space key. mpv can
    // unpause and re-pause at EOF within one turn, so observing pause alone
    // cannot implement replay. Send simple playback actions through the
    // same controller path as the UI while retaining input.conf/remaps.
    const QString action = commandForKey(name).simplified();
    if (action == QStringLiteral("cycle pause")
        || action == QStringLiteral("playlist-next")
        || action == QStringLiteral("playlist-prev")
        || action == QStringLiteral("set pause no")
        || action == QStringLiteral("set pause yes")
        || action == QStringLiteral("frame-step")
        || action == QStringLiteral("frame-back-step")
        || action == QStringLiteral("cycle sub")
        || action == QStringLiteral("cycle sid")
        || action == QStringLiteral("cycle sub-visibility")) {
        m_player->command(action.split(QLatin1Char(' ')));
        return true;
    }
    m_player->command({QStringLiteral("keypress"), name});
    return true;
}

bool ShortcutManager::handleWheel(int deltaY, Qt::KeyboardModifiers mods)
{
    Q_UNUSED(mods);
    if (deltaY == 0 || !m_player)
        return false;
    const QString name = deltaY > 0 ? QStringLiteral("WHEEL_UP") : QStringLiteral("WHEEL_DOWN");
    m_player->command({QStringLiteral("keypress"), name});
    return true;
}
