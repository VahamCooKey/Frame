#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantMap>

class PlayerController;
class SettingsStore;

// The engine owns the queue. This model only filters its view and resolves
// user actions against mpv's stable entry IDs immediately before dispatch.
class PlaylistSession final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectionChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY modelUpdated)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY modelUpdated)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentIdChanged)
    Q_PROPERTY(bool restoreEnabled READ restoreEnabled WRITE setRestoreEnabled NOTIFY restoreEnabledChanged)
    Q_PROPERTY(bool restoring READ restoring NOTIFY restoringChanged)

public:
    enum Role { EntryRole = Qt::UserRole + 1 };
    explicit PlaylistSession(PlayerController *player, SettingsStore *settings,
                             const QString &sessionFile = {}, QObject *parent = nullptr);
    ~PlaylistSession() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QString selectedId() const { return m_selectedId; }
    void setSelectedId(const QString &id);
    int selectedRow() const;
    int totalCount() const { return int(m_entries.size()); }
    QString currentId() const { return m_currentId; }
    bool restoreEnabled() const { return m_restoreEnabled; }
    void setRestoreEnabled(bool enabled);
    bool restoring() const { return m_restoring; }

    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE int rowForId(const QString &id) const;
    Q_INVOKABLE QVariantMap entryForId(const QString &id) const;
    Q_INVOKABLE bool play(const QString &id);
    Q_INVOKABLE bool remove(const QString &id);
    // Earlier/later refers to neighbouring visible results when filtered.
    Q_INVOKABLE bool moveBy(const QString &id, int direction);
    Q_INVOKABLE bool moveRelative(const QString &id, const QString &anchorId, bool after);

    // Call ONCE after launch intent is known, before the QML startup open.
    // Pass true for any explicit launch input (even invalid input) or selftest.
    // Appends local media passively; playlist-current-pos restores selection
    // without loading, seeking, folder expansion or unpause. A later explicit
    // Play action activates that occurrence through PlayerController.
    bool restoreIfEmpty(bool explicitLaunch);
    Q_INVOKABLE bool saveNow();
    // Connect before PlayerController::prepareForShutdown on aboutToQuit.
    void finish();

signals:
    void queryChanged();
    void selectionChanged();
    void currentIdChanged();
    void restoreEnabledChanged();
    void restoringChanged();
    void modelAboutToUpdate();
    void modelUpdated();
    void errorOccurred(const QString &message);

private:
    struct Entry {
        QString id;
        QVariantMap values;
    };
    void synchronize();
    void synchronizeQueue(const QVariantList &queue);
    void updateView();
    void scheduleSave();
    void applyRestoreSetting(bool enabled);
    struct QueueAction {
        enum Kind { RelativeMove, StepMove, Play, Remove } kind;
        QString id;
        QString anchor;
        int direction{0};
        bool after{false};
    };
    bool enqueueAction(const QueueAction &action);
    void scheduleActionPump();
    void pumpActions();
    bool actionComplete(const QVariantList &queue) const;
    QVariantList liveQueue() const;
    static int indexOfId(const QVariantList &queue, const QString &id);
    static QString localMediaPath(const QString &path);

    PlayerController *m_player;
    SettingsStore *m_settings;
    QString m_sessionFile;
    QList<Entry> m_entries;
    QHash<QString, int> m_entryRows;
    QStringList m_visibleIds;
    QString m_query;
    QString m_selectedId;
    QString m_currentId;
    bool m_restoreEnabled{false};
    bool m_startupDecided{false};
    bool m_restoring{false};
    bool m_finished{false};
    int m_restoreCount{0};
    int m_restoreSelectedIndex{-1};
    bool m_restoreSelectionApplied{false};
    bool m_restoreArmed{false};
    bool m_restorePlayRequested{false};
    QTimer m_saveTimer;
    QTimer m_actionTimer;
    QTimer m_actionTimeoutTimer;
    QElapsedTimer m_actionElapsed;
    QList<QueueAction> m_actions;
    QStringList m_expectedOrder;
    QString m_expectedCurrent;
    bool m_waitingForAction{false};
    bool m_actionAcknowledged{false};
    quint64 m_actionReply{0};
    bool m_actionReplyReceived{false};
    bool m_actionFailed{false};
    bool m_pumpingActions{false};
    QByteArray m_lastSaved;
};
