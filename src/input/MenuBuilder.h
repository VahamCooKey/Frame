// CONTRACT v4 — owned by lead. Amend via CONTRACTS.md only.
// v4: bindingTable() — the flat, every-binding view of the same parse, for the F11 shortcut list (amendment A22, 2026-09-03).
// v3: leaf items gain a "key" field — the mpv key name from the same binding line as the #menu: comment (amendment A10, 2026-09-03).
#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>

class QFileSystemWatcher;
class QTimer;

class MenuBuilder : public QObject {
    Q_OBJECT
public:
    explicit MenuBuilder(QObject *parent = nullptr);

    // Single file. Kept for tests.
    Q_INVOKABLE QVariantList buildMenuFromConf(const QString &filePath);

    // Merged menu tree from several conf files; later files win for the same
    // "#menu:" path. Leaf item:   { text: QString, command: QString, key: QString }
    // Branch item:                { text: QString, children: QVariantList }
    // Leaf "key" is the mpv key name parsed from the same binding line as the
    // "#menu:" comment; branches do not carry it.
    Q_INVOKABLE QVariantList buildMenu(const QStringList &files);

    // A22: the flat view of the same parse — one row per binding, in file
    // order, a later file's line for the same key replacing the earlier one
    // in place (which is how mpv itself resolves a duplicate binding).
    // Row: { key: QString, command: QString, menuPath: QString }, menuPath
    // being "Playback > A-B Loop > Set A" or empty for a binding with no
    // "#menu:" comment — the shortcut list must be able to show every
    // rebindable action, not only the ones that reached the menu.
    Q_INVOKABLE QVariantList bindingTable(const QStringList &files);

    // Watch conf files for changes. Emits menuChanged() after a 250 ms
    // debounce, but only if the staged re-parse still yields a non-empty
    // menu (a half-written/emptied file must not wipe the menu). Watched
    // paths are re-added after every change because editors often
    // delete-and-recreate on save.
    Q_INVOKABLE void watchFiles(const QStringList &paths);

signals:
    void menuChanged();

private:
    QStringList m_watched;
    QFileSystemWatcher *m_watcher{nullptr}; // created lazily by watchFiles
    QTimer *m_debounce{nullptr};            // created lazily by watchFiles
};
