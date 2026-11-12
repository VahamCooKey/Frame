// CONTRACT v6 — owned by lead. Amend via CONTRACTS.md only.
// v6: amendment A33 — generic value()/setValue(), keyed "group/name" with a
//     caller-supplied default. Backs the Part-3 settings restructure (every
//     new pane setting reads/writes through this instead of a bespoke
//     accessor pair per setting, which is what kept the store at 9 keys
//     through five amendments). ytdl/* accessors removed with yt-dlp (A31).
// v5: amendments A22 (F11 shortcut overrides, one flat shortcuts/overrides
//     list) and A23 (F47 yt-dlp mode and format). Both are new INI groups;
//     nothing migrates.
// v4: amendment A19 — per-file bookmarks (F6e), stored under
//     bookmarks/<md5 of normalized path> as a sorted second list.
// v2: appliedShaders persistence (amendment A5).
// v3: window geometry + volume persistence (amendment A6).
#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class SettingsStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(QStringList shaderFiles READ appliedShaders WRITE setAppliedShaders NOTIFY appliedShadersChanged)

public:
    explicit SettingsStore(QObject *parent = nullptr);

    QVariantList recentFiles() const;
    Q_INVOKABLE void clearRecentFiles();

    Q_INVOKABLE void addRecent(const QString &filePath);
    Q_INVOKABLE double resumePosition(const QString &filePath) const;
    Q_INVOKABLE void saveResume(const QString &filePath, double position);
    Q_INVOKABLE void clearResume(const QString &filePath);

    // F6e bookmarks: finite, nonnegative positions in seconds, always returned sorted
    // ascending. Keyed by the same normalized-path md5 as resume, so a
    // file keeps its marks across separator and case differences.
    Q_INVOKABLE QVariantList bookmarks(const QString &filePath) const;
    // Adding within kBookmarkEpsilon of an existing mark REMOVES it, so
    // one key toggles a bookmark at the playhead. Returns true if a
    // mark was added, false if one was removed or the input was invalid.
    Q_INVOKABLE bool toggleBookmark(const QString &filePath, double position);
    Q_INVOKABLE void clearBookmarks(const QString &filePath);
    Q_INVOKABLE QStringList appliedShaders() const;
    Q_INVOKABLE void setAppliedShaders(const QStringList &shaders);
    // Session state: last window rectangle ({x,y,width,height}; empty map =
    // never saved) and last volume (-1 = never saved).
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void setWindowGeometry(const QVariantMap &geometry);
    Q_INVOKABLE double volume() const;
    Q_INVOKABLE void setVolume(double volume);

    // F11 (A22) shortcut overrides, keyed by the mpv command — the identity
    // of an action that survives a rebind. An empty key means the action is
    // DISABLED, which is not the same as having no override for it.
    Q_INVOKABLE QVariantMap shortcutOverrides() const;
    Q_INVOKABLE void setShortcutOverride(const QString &command, const QString &key);
    Q_INVOKABLE void clearShortcutOverride(const QString &command);
    Q_INVOKABLE void clearShortcutOverrides();

    // Known preferences share one typed default for startup, live apply,
    // controls and reset. The caller's fallback remains useful for other
    // state under settings/* (for example onboarding and queue restore).
    static const QVariantMap &defaults();
    Q_INVOKABLE QVariant defaultValue(const QString &key) const;
    Q_INVOKABLE bool contains(const QString &key) const;
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    // A stored boolean. The INI hands booleans back as the strings "true"/
    // "false", and QML treats any non-empty string as true, so QML must read
    // on/off settings through this rather than value().
    Q_INVOKABLE bool flag(const QString &key, bool defaultValue = false) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    // One preference back to its typed default: the stored value is removed
    // (so a later default change applies) and settingChanged publishes the
    // default to live listeners exactly like a user edit would.
    Q_INVOKABLE void resetSetting(const QString &key);
    // True when the preference currently reads as its default, whether
    // nothing is stored or the stored value equals the default.
    Q_INVOKABLE bool isDefault(const QString &key) const;
    // Clears preferences and the applied shader list, then publishes typed
    // defaults to existing listeners. Recents, resume points, bookmarks,
    // window/volume state and shortcut overrides are retained.
    Q_INVOKABLE void resetAllSettings();
    // Advanced pane "Open log folder": %APPDATA%\Frame\logs, same directory
    // main.cpp's FileLogger writes to.
    Q_INVOKABLE void openLogFolder() const;

signals:
    void recentFilesChanged();
    void shortcutOverridesChanged();
    void appliedShadersChanged();
    void settingsReset();
    // Also emitted with each known default after reset. Unknown keys that
    // were cleared emit an invalid QVariant. All storage is already reset
    // when listeners run, so dependent reads see a consistent snapshot.
    void settingChanged(const QString &key, const QVariant &value);

private:
    QString keyFor(const QString &filePath) const;
    // md5 of the normalized path, shared by the resume and bookmark keys.
    QString digestFor(const QString &filePath) const;
    QStringList loadRecents() const;
    void saveRecents(const QStringList &files);
};
