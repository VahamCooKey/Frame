// CONTRACT v7 — owned by lead. Amend via CONTRACTS.md only.
// v7: amendment A22 — F11 rebinding. mpv keeps owning the binding table and
//     we only shadow it through its own "keybind" command; there is no
//     keyunbind, so releasing a key is "keybind <key> ignore".
//     toMpvKeyName becomes Q_INVOKABLE so the capture widget names a key
//     with the same function the filter matches against.
// v6: amendment A19 — textInputActive. The filter forwards every key to
//     mpv regardless of focus, so a QML text field (F6b go-to-time) would
//     have its digits eaten as shortcuts ("0" seeks to the start). While
//     set, NOTHING is forwarded and Qt delivers keys to the focused item.
// v5: amendment A15 — J/L are 0.25x speed steppers. A quick L tap (released
//     inside the hold window) emits shuttleStepUp(); the K+L slow-shuttle
//     (slowShuttleStep, A12) is removed because J now covers slowing down.
//     Hold detection (shuttleHoldStart/End, A14) unchanged. J/K themselves
//     are plain mpv app-commands again (app-shuttle-down / app-toggle-pause).
// v4: amendment A14 — the L key is no longer forwarded to mpv at all. The
//     filter counts L taps and runs a hold timer: shuttleHoldStart(taps) /
//     shuttleHoldEnd() drive the QML hold-shuttle (2.5x / 5x / 8x while
//     held, 0.25x ramp, release reverts). K+L slowShuttleStep unchanged.
// v3: amendment A13 — while a popup layer owns the keyboard, non-navigation
//     shortcuts are no longer dead: dismissMenusRequested() lets QML close
//     the pickers and the key then forwards normally.
#pragma once
#include <QObject>
#include <QMap>
#include <QEvent>
#include <QVariantList>
#include <QVariantMap>

class QTimer;
class PlayerController;
class QWindow;

class ShortcutManager : public QObject {
    Q_OBJECT
    // True while a Qt popup layer owns the interaction (context menu, track
    // picker). Keys are not forwarded to mpv then — except ESC, which peels
    // one layer — so arrow-key menu navigation cannot seek the video.
    Q_PROPERTY(bool menuOpen READ menuOpen WRITE setMenuOpen NOTIFY menuOpenChanged)
    // True while the cursor is over a scrollable view (playlist drawer rows,
    // track picker); wheel is not forwarded, so scrolling does not change
    // the volume.
    Q_PROPERTY(bool wheelOverScroller READ wheelOverScroller WRITE setWheelOverScroller NOTIFY wheelOverScrollerChanged)
    // True while a QML text field owns the keyboard. Unlike menuOpen this
    // suppresses EVERY key (ESC included — the field closes itself), because
    // any forwarded key would be both typed and acted on.
    Q_PROPERTY(bool textInputActive READ textInputActive WRITE setTextInputActive NOTIFY textInputActiveChanged)
    Q_PROPERTY(QVariantMap commandKeys READ commandKeys NOTIFY overridesChanged)

public:
    explicit ShortcutManager(QObject *parent = nullptr);

    // Qt key + modifiers -> mpv key name ("SPACE", "Ctrl+r", "F1"). Empty on
    // no match. Q_INVOKABLE (A22) so the key-capture widget spells a captured
    // key exactly the way the event filter does — two spellings of the same
    // key would bind one and fire the other.
    Q_INVOKABLE static QString toMpvKeyName(int key, Qt::KeyboardModifiers mods);

    void setPlayer(PlayerController *player);
    void installFilter(QWindow *window);
    Q_INVOKABLE bool handleKeyPress(int key, Qt::KeyboardModifiers mods);
    Q_INVOKABLE bool handleWheel(int deltaY, Qt::KeyboardModifiers mods);

    // A22: the input.conf table as MenuBuilder::bindingTable() rows, so a
    // rebind knows which key to release and a reset knows what to restore.
    Q_INVOKABLE void setDefaultBindings(const QVariantList &rows);
    // command -> key; an empty key disables the action. Applied through mpv's
    // own "keybind", and re-applying an empty map restores input.conf.
    Q_INVOKABLE void applyOverrides(const QVariantMap &overrides);
    Q_INVOKABLE QVariantMap overrides() const { return m_overrides; }
    // The command currently bound to <key>, or empty — the capture UI's
    // conflict check, so it answers from the overrides already applied and
    // not from input.conf alone.
    Q_INVOKABLE QString commandForKey(const QString &key) const;
    Q_INVOKABLE QString keyForCommand(const QString &command) const;
    QVariantMap commandKeys() const;

    bool menuOpen() const { return m_menuOpen; }
    void setMenuOpen(bool open);
    bool wheelOverScroller() const { return m_wheelOverScroller; }
    void setWheelOverScroller(bool over);
    bool textInputActive() const { return m_textInputActive; }
    void setTextInputActive(bool active);

signals:
    void menuOpenChanged(bool open);
    void wheelOverScrollerChanged(bool over);
    void textInputActiveChanged(bool active);
    // F20 (A12): emitted for a plain L KeyPress while plain K is held, in
    // place of forwarding L to mpv. main.qml owns the 0.5x -> 0.25x -> 0.125x
    // slow-shuttle ramp and steps it on this signal.
    void slowShuttleStep();
    // F27 (A13): emitted when a non-navigation key arrives while a popup
    // layer owns the keyboard. QML closes the track pickers synchronously in
    // the handler (which clears menuOpen); the filter then forwards the key
    // normally, so e.g. F closes fullscreen while a subtitle menu is open.
    // Menu-navigation keys (arrows/Enter/Tab) and ESC keep their dedicated
    // handling and never raise this.
    void dismissMenusRequested();
    // F30 (A14) / F36 (A15): hold-shuttle. shuttleHoldStart fires once the L
    // key has been HELD past the tap window (220 ms); <taps> is the
    // rapid-tap count leading into the hold (1 = 2.5x, 2 = 5x, 3 = 8x — QML
    // owns the table and the 0.25x ramp). shuttleHoldEnd fires on release;
    // QML reverts to the pre-hold speed and restores the pre-hold pause
    // state. shuttleStepUp fires for a quick L tap (released inside the
    // window): QML steps the speed up by 0.25x.
    void shuttleHoldStart(int taps);
    void shuttleHoldEnd();
    void shuttleStepUp();
    // F11 (A22): the menu's shortcut column follows a rebind without a restart.
    void overridesChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void bind(const QString &key, const QString &command);

    PlayerController *m_player{nullptr};
    bool m_menuOpen{false};
    bool m_wheelOverScroller{false};
    bool m_textInputActive{false};
    // A14 hold-shuttle state (plain L only): rapid-tap count (1..3) leading
    // into a hold, press/release bookkeeping for the tap window, and the
    // hold timer that separates "tap" from "hold".
    int m_lTaps{0};
    qint64 m_lastLReleaseMs{0};
    bool m_lPressed{false};
    bool m_holdActive{false};
    QTimer *m_holdTimer{nullptr};
    // A22: input.conf read both ways. command -> key is what a rebind
    // releases and a reset restores; key -> command answers the conflict
    // check and says who owns a key handed back.
    QMap<QString, QString> m_defaultKeyFor;
    QMap<QString, QString> m_defaultCommandFor;
    // Every key input.conf gives a command, not just the first. Five commands
    // are bound twice (SPACE and PLAYPAUSE both pause; UP and = both raise the
    // volume), and releasing only the primary left the twin still firing an
    // action the user had just disabled.
    QMap<QString, QStringList> m_defaultKeysFor;
    // The overrides currently shadowing input.conf (empty key = disabled).
    // Held until a player is attached — settings are restored before mpv is.
    QVariantMap m_overrides;
};
