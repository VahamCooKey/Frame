#include "PlaylistSession.h"

#include "PlayerController.h"
#include "SettingsStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSet>
#include <QUrl>

namespace {
constexpr auto kRestoreKey = "general/restorePlaylist";
constexpr int kMaxSessionEntries = 10000;
constexpr qint64 kMaxSessionBytes = 4 * 1024 * 1024;
constexpr int kActionTimeoutMs = 2000;

QString entryId(const QVariant &value)
{
    // IDs remain strings in QML, including IDs outside JS's exact integer range.
    bool valid = false;
    const qint64 id = value.toMap().value(QStringLiteral("id")).toLongLong(&valid);
    return valid && id >= 0 ? QString::number(id) : QString();
}
} // namespace

PlaylistSession::PlaylistSession(PlayerController *player, SettingsStore *settings,
                                 const QString &sessionFile, QObject *parent)
    : QAbstractListModel(parent), m_player(player), m_settings(settings), m_sessionFile(sessionFile)
{
    if (m_sessionFile.isEmpty()) {
        const QSettings preferences(QSettings::IniFormat, QSettings::UserScope,
                                    QStringLiteral("Frame"), QStringLiteral("Frame"));
        m_sessionFile = QFileInfo(preferences.fileName()).absolutePath()
                      + QStringLiteral("/playlist-session.json");
    }
    m_restoreEnabled = m_settings && m_settings->flag(QLatin1String(kRestoreKey), false);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &PlaylistSession::saveNow);
    // Notifications coalesce work onto the next turn, outside the controller's
    // event drain. A separate, fixed deadline handles commands with no change
    // notification (including failures); it never polls the backend.
    m_actionTimer.setSingleShot(true);
    m_actionTimer.setInterval(0);
    connect(&m_actionTimer, &QTimer::timeout, this, &PlaylistSession::pumpActions);
    m_actionTimeoutTimer.setSingleShot(true);
    m_actionTimeoutTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_actionTimeoutTimer, &QTimer::timeout, this, &PlaylistSession::pumpActions);
    if (m_player) {
        connect(m_player, &PlayerController::playlistChanged, this, &PlaylistSession::synchronize);
        connect(m_player, &PlayerController::playlistPosChanged, this, &PlaylistSession::synchronize);
        connect(m_player, &PlayerController::unavailableFilesChanged, this, &PlaylistSession::synchronize);
        connect(m_player, &PlayerController::commandFinished, this,
                [this](quint64 id, bool success, const QString &) {
            if (m_finished || !m_waitingForAction || id != m_actionReply) return;
            m_actionReply = 0;
            m_actionReplyReceived = true;
            m_actionFailed = !success;
            if (success && actionComplete(m_player->playlist())) m_actionAcknowledged = true;
            scheduleActionPump();
        });
        connect(m_player, &PlayerController::fileLoaded, this, [this] {
            m_restoreArmed = false;
            m_restorePlayRequested = false;
            synchronize();
        });
        connect(m_player, &PlayerController::playbackActionRequested, this, [this](bool paused) {
            if (paused || !m_restoreArmed) return;
            if (m_restoring) m_restorePlayRequested = true;
            else if (m_player->mpvProperty(QStringLiteral("idle-active")).toBool()) {
                m_restoreArmed = false;
                play(m_selectedId);
            }
        });
    }
    if (m_settings) {
        connect(m_settings, &SettingsStore::settingChanged, this,
                [this](const QString &key, const QVariant &) {
            if (key == QLatin1String(kRestoreKey))
                applyRestoreSetting(m_settings->flag(QLatin1String(kRestoreKey), false));
        });
    }
    synchronize();
}

PlaylistSession::~PlaylistSession() { finish(); }

int PlaylistSession::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_visibleIds.size());
}

QVariant PlaylistSession::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleIds.size() || role != EntryRole)
        return {};
    return entryForId(m_visibleIds.at(index.row()));
}

QHash<int, QByteArray> PlaylistSession::roleNames() const { return {{EntryRole, "entry"}}; }

QVariantMap PlaylistSession::entryForId(const QString &id) const
{
    const auto found = m_entryRows.constFind(id);
    return found == m_entryRows.cend() ? QVariantMap() : m_entries.at(found.value()).values;
}

QString PlaylistSession::idAt(int row) const
{
    return row >= 0 && row < m_visibleIds.size() ? m_visibleIds.at(row) : QString();
}

int PlaylistSession::rowForId(const QString &id) const { return int(m_visibleIds.indexOf(id)); }
int PlaylistSession::selectedRow() const { return rowForId(m_selectedId); }

void PlaylistSession::setQuery(const QString &query)
{
    if (m_query == query) return;
    emit modelAboutToUpdate();
    m_query = query;
    emit queryChanged();
    updateView();
}

void PlaylistSession::setSelectedId(const QString &id)
{
    if (m_selectedId == id || (!id.isEmpty() && entryForId(id).isEmpty())) return;
    m_selectedId = id;
    emit selectionChanged();
    scheduleSave();
}

void PlaylistSession::setRestoreEnabled(bool enabled)
{
    if (m_settings) m_settings->setValue(QLatin1String(kRestoreKey), enabled);
    else applyRestoreSetting(enabled);
}

void PlaylistSession::applyRestoreSetting(bool enabled)
{
    if (m_restoreEnabled == enabled) return;
    m_restoreEnabled = enabled;
    emit restoreEnabledChanged();
    if (enabled) scheduleSave();
    else m_saveTimer.stop();
}

QVariantList PlaylistSession::liveQueue() const
{
    return m_player ? m_player->mpvProperty(QStringLiteral("playlist")).toList() : QVariantList();
}

int PlaylistSession::indexOfId(const QVariantList &queue, const QString &id)
{
    if (id.isEmpty()) return -1;
    for (qsizetype i = 0; i < queue.size(); ++i)
        if (entryId(queue.at(i)) == id) return int(i);
    return -1;
}

bool PlaylistSession::play(const QString &id)
{
    return enqueueAction({QueueAction::Play, id, {}});
}

bool PlaylistSession::remove(const QString &id)
{
    return enqueueAction({QueueAction::Remove, id, {}});
}

bool PlaylistSession::moveBy(const QString &id, int direction)
{
    const int row = rowForId(id);
    const int neighbour = row + (direction < 0 ? -1 : 1);
    if (direction == 0 || row < 0 || neighbour < 0 || neighbour >= m_visibleIds.size()) return false;
    return enqueueAction({QueueAction::StepMove, id, {}, direction});
}

bool PlaylistSession::moveRelative(const QString &id, const QString &anchorId, bool after)
{
    const auto queue = liveQueue();
    const int from = indexOfId(queue, id);
    const int anchor = indexOfId(queue, anchorId);
    if (id == anchorId || from < 0 || anchor < 0) return false;
    const int destination = anchor + (after ? 1 : 0);
    if (!m_waitingForAction && m_actions.isEmpty() && (destination == from || destination == from + 1)) return false;
    return enqueueAction({QueueAction::RelativeMove, id, anchorId, 0, after});
}

bool PlaylistSession::enqueueAction(const QueueAction &action)
{
    if (m_finished || m_actions.size() >= 128) return false;
    const auto queue = liveQueue();
    if (indexOfId(queue, action.id) < 0) return false;
    // This explicit action boundary also sees completed mutations whose Qt
    // notification is still queued. Do not substitute the observed cache here.
    if (m_waitingForAction && actionComplete(queue)) m_actionAcknowledged = true;
    if (action.kind != QueueAction::Remove) setSelectedId(action.id);
    m_actions.append(action);
    if (!m_waitingForAction) {
        if (m_pumpingActions) scheduleActionPump();
        else pumpActions();
    } else if (m_actionAcknowledged) scheduleActionPump();
    return true;
}

void PlaylistSession::scheduleActionPump()
{
    if (!m_finished && (m_waitingForAction || !m_actions.isEmpty()) && !m_actionTimer.isActive())
        m_actionTimer.start();
}

bool PlaylistSession::actionComplete(const QVariantList &queue) const
{
    if (!m_expectedCurrent.isEmpty()) {
        // During a file switch, "playing" can still refer to the old entry.
        // Prefer the requested current entry, regardless of its queue index.
        QString playing;
        for (const auto &item : queue) {
            const auto map = item.toMap();
            if (map.value(QStringLiteral("current")).toBool())
                return entryId(item) == m_expectedCurrent;
            if (map.value(QStringLiteral("playing")).toBool()) playing = entryId(item);
        }
        return playing == m_expectedCurrent;
    }
    if (queue.size() != m_expectedOrder.size()) return false;
    for (qsizetype i = 0; i < queue.size(); ++i)
        if (entryId(queue.at(i)) != m_expectedOrder.at(i)) return false;
    return true;
}

void PlaylistSession::pumpActions()
{
    if (m_finished || m_pumpingActions) return;
    const QScopedValueRollback pumping(m_pumpingActions, true);
    // A second async index command must wait for the first mutation to land.
    // Otherwise rapid keyboard repeats can move/remove a different duplicate.
    if (m_waitingForAction) {
        if (m_actionFailed || !m_actionReplyReceived || !m_actionAcknowledged) {
            const qint64 remaining = kActionTimeoutMs - m_actionElapsed.elapsed();
            if (!m_actionFailed && remaining > 0) {
                // Only needed if a timer is delivered early. Observations and
                // repeated user input never extend the absolute deadline.
                if (!m_actionTimeoutTimer.isActive()) m_actionTimeoutTimer.start(int(remaining));
                return;
            }
            // One final reconciliation at the deadline, not a periodic poll.
            // A no-change selection or coalesced-away notification must not
            // report failure when the engine has already reached the target.
            const auto queue = liveQueue();
            m_actionAcknowledged = m_actionReplyReceived && !m_actionFailed && actionComplete(queue);
            if (!m_actionAcknowledged) {
                m_actions.clear();
                m_expectedOrder.clear();
                m_expectedCurrent.clear();
                m_waitingForAction = false;
                m_actionReply = 0;
                m_actionReplyReceived = false;
                m_actionFailed = false;
                m_actionTimer.stop();
                m_actionTimeoutTimer.stop();
                synchronizeQueue(queue);
                emit errorOccurred(QStringLiteral("The playlist changed before the action could finish. Try again."));
                return;
            }
            synchronizeQueue(queue);
            if (m_finished) return;
        }
        m_expectedOrder.clear();
        m_expectedCurrent.clear();
        m_waitingForAction = false;
        m_actionAcknowledged = false;
        m_actionReplyReceived = false;
        m_actionFailed = false;
        m_actionTimeoutTimer.stop();
    }
    while (!m_actions.isEmpty()) {
        const QueueAction action = m_actions.takeFirst();
        // Resolve stable IDs against the engine once at dispatch. An external
        // mutation can have completed since the last observed/coalesced update;
        // using that cached order here could operate on a different duplicate.
        const auto live = liveQueue();
        const int from = indexOfId(live, action.id);
        if (from < 0) continue;
        QStringList expected;
        for (const auto &item : live) expected.append(entryId(item));
        int destination = -1;
        if (action.kind == QueueAction::Play) {
            m_expectedCurrent = action.id;
        } else if (action.kind == QueueAction::Remove) {
            expected.removeAt(from);
            m_expectedOrder = expected;
        } else {
            int anchor = indexOfId(live, action.anchor);
            bool after = action.after;
            if (action.kind == QueueAction::StepMove) {
                QStringList visible;
                const QString needle = m_query.trimmed();
                for (const auto &item : live) {
                    const auto map = item.toMap();
                    if (needle.isEmpty() || map.value(QStringLiteral("filename")).toString().contains(needle, Qt::CaseInsensitive)
                        || map.value(QStringLiteral("title")).toString().contains(needle, Qt::CaseInsensitive))
                        visible.append(entryId(item));
                }
                const int row = int(visible.indexOf(action.id));
                const int neighbour = row + (action.direction < 0 ? -1 : 1);
                if (row < 0 || neighbour < 0 || neighbour >= visible.size()) continue;
                anchor = indexOfId(live, visible.at(neighbour));
                after = action.direction > 0;
            }
            if (anchor < 0) continue;
            // mpv's destination is a boundary in the ORIGINAL queue.
            destination = anchor + (after ? 1 : 0);
            if (destination == from || destination == from + 1) continue;
            expected.move(from, destination - (from < destination ? 1 : 0));
            m_expectedOrder = expected;
        }
        m_waitingForAction = true;
        m_actionAcknowledged = false;
        m_actionReplyReceived = false;
        m_actionFailed = false;
        m_actionElapsed.start();
        m_actionTimeoutTimer.start(kActionTimeoutMs);
        if (action.kind == QueueAction::Play) m_actionReply = m_player->playlistPlayIndex(from);
        else if (action.kind == QueueAction::Remove) m_actionReply = m_player->playlistRemove(from);
        else m_actionReply = m_player->command({QStringLiteral("playlist-move"), QString::number(from), QString::number(destination)});
        if (!m_actionReply) {
            m_actionFailed = true; // Immediate submission failure has no future reply.
            scheduleActionPump();
        }
        return;
    }
    m_actionTimer.stop();
}

void PlaylistSession::synchronize()
{
    if (!m_player) return;
    const auto queue = m_player->playlist();
    // Remember an acknowledgement before emitting model signals: another
    // observer can change the queue, or EOF can advance selection, before the
    // coalesced pump runs. The next command still resolves a fresh order.
    if (m_waitingForAction && actionComplete(queue)) m_actionAcknowledged = true;
    synchronizeQueue(queue);
    scheduleActionPump();
}

void PlaylistSession::synchronizeQueue(const QVariantList &queue)
{
    if (!m_player) return;
    emit modelAboutToUpdate();
    const auto unavailable = m_player->unavailableFiles();
    QList<Entry> entries;
    entries.reserve(queue.size());
    QString current;
    QString queueCurrent;
    for (qsizetype i = 0; i < queue.size(); ++i) {
        auto values = queue.at(i).toMap();
        const QString id = entryId(queue.at(i));
        if (id.isEmpty()) continue;
        QString file = values.value(QStringLiteral("filename")).toString();
        const QUrl url(file);
        if (url.isLocalFile()) file = QDir::cleanPath(url.toLocalFile());
        else if (url.scheme().size() < 2) file = QDir::fromNativeSeparators(file);
        values.insert(QStringLiteral("filename"), file);
        values.insert(QStringLiteral("entryId"), id);
        values.insert(QStringLiteral("queueIndex"), int(i));
        const bool playing = values.value(QStringLiteral("playing")).toBool();
        values.insert(QStringLiteral("isCurrent"), playing);
        if (playing) current = id;
        if (values.value(QStringLiteral("current")).toBool()) queueCurrent = id;
        // Cache availability until the file is explicitly retried/loaded.
        // This runs on queue changes, never on playback position updates.
        const auto previous = entryForId(id);
        const QString local = localMediaPath(file);
        const bool loaded = !m_player->currentFile().isEmpty() && file == m_player->currentFile();
        const bool missing = !local.isEmpty() && !loaded && (previous.isEmpty()
            ? !QFileInfo::exists(local) : previous.value(QStringLiteral("missing")).toBool());
        values.insert(QStringLiteral("missing"), missing);
        values.insert(QStringLiteral("unavailable"), unavailable.contains(file) || missing);
        entries.append({id, values});
    }
    m_entries = std::move(entries);
    m_entryRows.clear();
    for (int i = 0; i < m_entries.size(); ++i) m_entryRows.insert(m_entries.at(i).id, i);
    if (m_currentId != current) {
        m_currentId = current;
        emit currentIdChanged();
    }
    if (m_restoring && m_entries.size() >= m_restoreCount) {
        if (!current.isEmpty()) {
            // A new explicit open while passive appends were pending wins.
            m_restoring = false;
            emit restoringChanged();
        } else if (m_restoreSelectedIndex >= 0 && m_restoreSelectedIndex < m_entries.size()) {
            m_selectedId = m_entries.at(m_restoreSelectedIndex).id;
            if (queueCurrent == m_selectedId) {
                m_restoring = false;
                emit restoringChanged();
            } else if (!m_restoreSelectionApplied) {
                // Unlike playlist-pos, this property never starts loading in
                // idle mode. Play/Space will subsequently use this occurrence.
                m_restoreSelectionApplied = true;
                m_player->command({QStringLiteral("set"), QStringLiteral("playlist-current-pos"),
                                   QString::number(m_restoreSelectedIndex)});
            }
        }
    }
    if (entryForId(m_selectedId).isEmpty())
        m_selectedId = !current.isEmpty() ? current : !queueCurrent.isEmpty() ? queueCurrent
            : (m_entries.isEmpty() ? QString() : m_entries.first().id);
    updateView();
    scheduleSave();
    if (!m_restoring && m_restoreArmed && m_restorePlayRequested) {
        m_restoreArmed = false;
        m_restorePlayRequested = false;
        QTimer::singleShot(0, this, [this] { play(m_selectedId); });
    }
}

void PlaylistSession::updateView()
{
    QStringList wanted;
    const QString needle = m_query.trimmed();
    for (const auto &entry : m_entries) {
        const auto &values = entry.values;
        if (needle.isEmpty()
            || values.value(QStringLiteral("filename")).toString().contains(needle, Qt::CaseInsensitive)
            || values.value(QStringLiteral("title")).toString().contains(needle, Qt::CaseInsensitive))
            wanted.append(entry.id);
    }
    // Incremental moves preserve QML delegates, current-item identity and
    // persistent model indexes. Filtering never mutates the engine's queue.
    const QSet<QString> retained(wanted.cbegin(), wanted.cend());
    for (int i = int(m_visibleIds.size()) - 1; i >= 0; --i) {
        if (retained.contains(m_visibleIds.at(i))) continue;
        beginRemoveRows({}, i, i);
        m_visibleIds.removeAt(i);
        endRemoveRows();
    }
    // Moving an early card to the end of a long queue is one model move,
    // rather than moving every intervening delegate past it individually.
    if (m_visibleIds.size() == wanted.size()) {
        int first = 0;
        while (first < wanted.size() && m_visibleIds.at(first) == wanted.at(first)) ++first;
        int last = int(wanted.size()) - 1;
        while (last > first && m_visibleIds.at(last) == wanted.at(last)) --last;
        if (first < last && m_visibleIds.at(first) == wanted.at(last)) {
            bool forwardMove = true;
            for (int i = first; i < last && forwardMove; ++i)
                forwardMove = m_visibleIds.at(i + 1) == wanted.at(i);
            if (forwardMove) {
                beginMoveRows({}, first, first, {}, last + 1);
                m_visibleIds.move(first, last);
                endMoveRows();
            }
        }
    }
    for (int i = 0; i < wanted.size(); ++i) {
        if (i < m_visibleIds.size() && m_visibleIds.at(i) == wanted.at(i)) continue;
        const int previous = int(m_visibleIds.indexOf(wanted.at(i), i));
        if (previous >= 0) {
            beginMoveRows({}, previous, previous, {}, i);
            m_visibleIds.move(previous, i);
            endMoveRows();
        } else {
            beginInsertRows({}, i, i);
            m_visibleIds.insert(i, wanted.at(i));
            endInsertRows();
        }
    }
    if (!m_visibleIds.isEmpty()) emit dataChanged(index(0), index(int(m_visibleIds.size()) - 1), {EntryRole});
    emit selectionChanged();
    emit modelUpdated();
}

QString PlaylistSession::localMediaPath(const QString &path)
{
    QString local = path;
    const QUrl url(path);
    if (url.isLocalFile()) local = url.toLocalFile();
    else if (url.scheme().size() > 1) return {}; // Includes all streams and mpv pseudo protocols.
    local = QDir::fromNativeSeparators(local);
    if (!QDir::isAbsolutePath(local) || local.startsWith(QStringLiteral("//"))) return {};
    static const QSet<QString> suffixes{
        QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("wmv"), QStringLiteral("flv"),
        QStringLiteral("ts"), QStringLiteral("m2ts"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
        QStringLiteral("ogv"), QStringLiteral("3gp"), QStringLiteral("mp3"), QStringLiteral("m4a"),
        QStringLiteral("flac"), QStringLiteral("wav"), QStringLiteral("aac"), QStringLiteral("ogg"),
        QStringLiteral("opus")};
    const QFileInfo info(local);
    if (!suffixes.contains(info.suffix().toLower()) || info.isDir()) return {};
    return QDir::cleanPath(local);
}

bool PlaylistSession::restoreIfEmpty(bool explicitLaunch)
{
    if (m_startupDecided) return false;
    m_startupDecided = true;
    if (explicitLaunch || !m_restoreEnabled || !m_player || !liveQueue().isEmpty()
        || !m_player->currentFile().isEmpty()) return false;
    QFile file(m_sessionFile);
    if (!file.open(QIODevice::ReadOnly)) return false;
    if (file.size() > kMaxSessionBytes) return false;
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto saved = document.object();
    if (saved.value(QStringLiteral("version")).toInt() != 1) return false;
    const auto savedEntries = saved.value(QStringLiteral("entries")).toArray();
    if (savedEntries.size() > kMaxSessionEntries) return false;
    const int selected = saved.value(QStringLiteral("selectedIndex")).toInt(-1);
    QStringList paths;
    int selectedIndex = -1;
    for (qsizetype i = 0; i < savedEntries.size(); ++i) {
        const QString path = localMediaPath(savedEntries.at(i).toObject().value(QStringLiteral("filename")).toString());
        if (path.isEmpty()) continue;
        if (i == selected) selectedIndex = int(paths.size());
        paths.append(path);
    }
    if (paths.isEmpty()) return false;
    m_restoreCount = int(paths.size());
    m_restoreSelectedIndex = selectedIndex >= 0 ? selectedIndex : 0;
    m_restoring = true;
    m_restoreArmed = true;
    emit restoringChanged();
    m_player->playlistAppendFiles(paths, false);
    return true;
}

void PlaylistSession::scheduleSave()
{
    if (m_startupDecided && m_restoreEnabled && !m_restoring && !m_finished)
        m_saveTimer.start();
}

bool PlaylistSession::saveNow()
{
    m_saveTimer.stop();
    if (!m_startupDecided || !m_restoreEnabled || m_restoring || m_finished) return false;
    // The final 16ms coalesced playlist notification can still be pending
    // when the window closes. Persist the engine's completed order now.
    synchronizeQueue(liveQueue());
    m_saveTimer.stop();
    QJsonArray entries;
    int selectedIndex = -1;
    int currentIndex = -1;
    for (const auto &entry : m_entries) {
        const QString path = localMediaPath(entry.values.value(QStringLiteral("filename")).toString());
        if (path.isEmpty()) continue;
        if (entry.id == m_selectedId) selectedIndex = int(entries.size());
        if (entry.id == m_currentId) currentIndex = int(entries.size());
        entries.append(QJsonObject{{QStringLiteral("filename"), path}});
        if (entries.size() >= kMaxSessionEntries) break;
    }
    if (selectedIndex < 0) selectedIndex = currentIndex;
    const QByteArray json = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1}, {QStringLiteral("entries"), entries},
        {QStringLiteral("selectedIndex"), selectedIndex}}).toJson(QJsonDocument::Compact);
    if (json == m_lastSaved) return true;
    if (!QDir().mkpath(QFileInfo(m_sessionFile).absolutePath())) {
        emit errorOccurred(QStringLiteral("Could not save the playlist session."));
        return false;
    }
    QSaveFile file(m_sessionFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        emit errorOccurred(QStringLiteral("Could not save the playlist session."));
        return false;
    }
    m_lastSaved = json;
    return true;
}

void PlaylistSession::finish()
{
    if (m_finished) return;
    saveNow();
    m_finished = true;
    m_saveTimer.stop();
    m_actionTimer.stop();
    m_actionTimeoutTimer.stop();
    m_actions.clear();
    m_waitingForAction = false;
    m_actionAcknowledged = false;
    m_actionReply = 0;
    m_actionReplyReceived = false;
    m_actionFailed = false;
    m_expectedOrder.clear();
    m_expectedCurrent.clear();
}
