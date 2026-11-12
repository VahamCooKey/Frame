#include "SettingsStore.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QSettings>
#include <QVariantList>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxRecents = 10;

QString organizationName()
{
    return QStringLiteral("Frame");
}

QString applicationName()
{
    return QStringLiteral("Frame");
}

// INI under %APPDATA%\Frame (inspectable, portable) instead of the
// Windows-registry default of QSettings(org, app).
QSettings makeSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     organizationName(), applicationName());
}

QStringList normalizedShaderPaths(const QStringList &shaders)
{
    QStringList result;
    for (const QString &shader : shaders) {
        const QString path = QDir::fromNativeSeparators(shader);
        if (!path.isEmpty() && !result.contains(path))
            result.append(path);
    }
    return result;
}

// The same file reaches us with different separators (QML file URLs give
// forward slashes, mpv's "path" can give native ones); normalize at the
// single choke point or recents dedupe and resume lookup both break.
QString normalizePath(const QString &filePath)
{
    const QUrl url(filePath);
    if (url.isValid() && url.isLocalFile())
        return QDir::cleanPath(url.toLocalFile());

    // F45: streams are stored too (recents, resume, bookmarks) and a URL is
    // not a path — QDir::cleanPath turns "https://host" into "https:/host",
    // which recorded a recent entry that could never be reopened. Mirrors
    // the same guard in PlayerController's normalizeLocalPath.
    if (url.isValid() && url.scheme().size() > 1 && !url.host().isEmpty())
        return filePath;

    return QDir::cleanPath(QDir::fromNativeSeparators(filePath));
}

// A22. The overrides live in ONE key as "<command>\x1F<key>" entries rather
// than a key per action: an mpv command is the identity of an action across
// rebinds but a terrible QSettings key — "/" opens a group, and "set speed
// 1.0" carries dots and spaces. Same dodge as the bookmark list.
constexpr char16_t kOverrideSeparator = u'\x1F';

QString overridesKey()
{
    return QStringLiteral("shortcuts/overrides");
}

void saveOverrides(const QVariantMap &overrides)
{
    QStringList raw;
    raw.reserve(overrides.size());
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        raw.append(it.key() + QChar(kOverrideSeparator) + it.value().toString());

    QSettings settings = makeSettings();
    if (raw.isEmpty())
        settings.remove(overridesKey());
    else
        settings.setValue(overridesKey(), raw);
    settings.sync();
}

} // namespace

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
{
}

QVariantList SettingsStore::recentFiles() const
{
    QVariantList result;
    const QStringList files = loadRecents();
    result.reserve(files.size());
    for (const QString &file : files)
        result.append(file);
    return result;
}

void SettingsStore::addRecent(const QString &filePath)
{
    const QString file = normalizePath(filePath);
    if (file.isEmpty())
        return;

    // General pane "recents length" (settings/general/recentsLimit); reread
    // on every add rather than cached, since the setting lives in the same
    // INI this call is about to write anyway. Clamped so a bad/hand-edited
    // value (0, negative, non-numeric -> toInt() 0) cannot empty the list.
    const int limit = std::max(1, value(QStringLiteral("general/recentsLimit"), kMaxRecents).toInt());

    QStringList files = loadRecents();
    files.removeAll(file);
    files.prepend(file);
    while (files.size() > limit)
        files.removeLast();
    saveRecents(files);

    emit recentFilesChanged();
}

void SettingsStore::clearRecentFiles()
{
    saveRecents({});
    emit recentFilesChanged();
}

double SettingsStore::resumePosition(const QString &filePath) const
{
    if (filePath.isEmpty())
        return 0.0;

    QSettings settings = makeSettings();
    return settings.value(keyFor(normalizePath(filePath)), 0.0).toDouble();
}

void SettingsStore::saveResume(const QString &filePath, double position)
{
    const QString file = normalizePath(filePath);
    if (file.isEmpty())
        return;

    QSettings settings = makeSettings();
    settings.setValue(keyFor(file), position);
    settings.sync();
}

void SettingsStore::clearResume(const QString &filePath)
{
    const QString file = normalizePath(filePath);
    if (file.isEmpty())
        return;

    QSettings settings = makeSettings();
    settings.remove(keyFor(file));
    settings.sync();
}

QStringList SettingsStore::appliedShaders() const
{
    QSettings settings = makeSettings();
    // Earlier versions stored picker paths verbatim. Normalize reads too,
    // so removing an old Windows path works before the next list is saved.
    return normalizedShaderPaths(settings.value(QStringLiteral("shaders/applied")).toStringList());
}

void SettingsStore::setAppliedShaders(const QStringList &shaders)
{
    const QStringList unique = normalizedShaderPaths(shaders);
    if (appliedShaders() == unique) {
        // The existing picker appends to mpv before saving its list. Still
        // notify on a reselected file so the coordinator removes duplicates
        // and honors a disabled shader list.
        emit appliedShadersChanged();
        return;
    }
    QSettings settings = makeSettings();
    settings.setValue(QStringLiteral("shaders/applied"), unique);
    settings.sync();
    emit appliedShadersChanged();
}

QVariantMap SettingsStore::windowGeometry() const
{
    QSettings settings = makeSettings();
    if (!settings.value(QStringLiteral("window/valid"), false).toBool())
        return {};
    QVariantMap geometry;
    geometry.insert(QStringLiteral("x"), settings.value(QStringLiteral("window/x")).toDouble());
    geometry.insert(QStringLiteral("y"), settings.value(QStringLiteral("window/y")).toDouble());
    geometry.insert(QStringLiteral("width"), settings.value(QStringLiteral("window/width")).toDouble());
    geometry.insert(QStringLiteral("height"), settings.value(QStringLiteral("window/height")).toDouble());
    return geometry;
}

void SettingsStore::setWindowGeometry(const QVariantMap &geometry)
{
    if (geometry.isEmpty())
        return;
    QSettings settings = makeSettings();
    settings.setValue(QStringLiteral("window/valid"), true);
    settings.setValue(QStringLiteral("window/x"), geometry.value(QStringLiteral("x")).toDouble());
    settings.setValue(QStringLiteral("window/y"), geometry.value(QStringLiteral("y")).toDouble());
    settings.setValue(QStringLiteral("window/width"), geometry.value(QStringLiteral("width")).toDouble());
    settings.setValue(QStringLiteral("window/height"), geometry.value(QStringLiteral("height")).toDouble());
    settings.sync();
}

double SettingsStore::volume() const
{
    QSettings settings = makeSettings();
    return settings.value(QStringLiteral("audio/volume"), -1.0).toDouble();
}

void SettingsStore::setVolume(double volume)
{
    if (volume < 0.0)
        return;
    QSettings settings = makeSettings();
    settings.setValue(QStringLiteral("audio/volume"), volume);
    settings.sync();
}

QVariantMap SettingsStore::shortcutOverrides() const
{
    QSettings settings = makeSettings();
    const QStringList raw = settings.value(overridesKey()).toStringList();

    QVariantMap overrides;
    for (const QString &entry : raw) {
        const qsizetype split = entry.indexOf(QChar(kOverrideSeparator));
        if (split <= 0)
            continue; // no command to key on; an empty key after it is legal
        overrides.insert(entry.left(split), entry.mid(split + 1));
    }
    return overrides;
}

void SettingsStore::setShortcutOverride(const QString &command, const QString &key)
{
    if (command.isEmpty())
        return;

    QVariantMap overrides = shortcutOverrides();
    overrides.insert(command, key);
    saveOverrides(overrides);

    emit shortcutOverridesChanged();
}

void SettingsStore::clearShortcutOverride(const QString &command)
{
    QVariantMap overrides = shortcutOverrides();
    if (overrides.remove(command) == 0)
        return;
    saveOverrides(overrides);

    emit shortcutOverridesChanged();
}

void SettingsStore::clearShortcutOverrides()
{
    QSettings settings = makeSettings();
    settings.remove(overridesKey());
    settings.sync();

    emit shortcutOverridesChanged();
}

const QVariantMap &SettingsStore::defaults()
{
    static const QVariantMap values{
        {QStringLiteral("general/resumeEnabled"), true},
        {QStringLiteral("general/folderAutoEnqueue"), true},
        {QStringLiteral("general/singleInstance"), true},
        {QStringLiteral("general/playlistRepeat"), false},
        {QStringLiteral("general/recentsLimit"), 10},
        {QStringLiteral("general/screenshotFormat"), QStringLiteral("png")},
        {QStringLiteral("general/screenshotDirectory"), QString()},
        {QStringLiteral("general/screenshotTemplate"), QStringLiteral("Frame_%F_%p")},
        {QStringLiteral("video/hwdec"), QStringLiteral("auto")},
        {QStringLiteral("video/aspectOverride"), QStringLiteral("no")},
        {QStringLiteral("video/deinterlace"), false},
        {QStringLiteral("video/videoSync"), QStringLiteral("display-resample")},
        {QStringLiteral("video/shadersEnabled"), true},
        {QStringLiteral("audio/device"), QStringLiteral("auto")},
        {QStringLiteral("audio/delayDefault"), 0.0},
        {QStringLiteral("audio/normalize"), false},
        {QStringLiteral("audio/preferredLanguage"), QString()},
        {QStringLiteral("subtitles/preferredLanguage"), QString()},
        {QStringLiteral("subtitles/fontSize"), 55},
        {QStringLiteral("subtitles/color"), QStringLiteral("#FFFFFFFF")},
        {QStringLiteral("subtitles/borderSize"), 3},
        {QStringLiteral("subtitles/position"), 100},
        {QStringLiteral("subtitles/delayDefault"), 0.0},
        {QStringLiteral("subtitles/autoSelect"), true},
        {QStringLiteral("subtitles/autoLoad"), QStringLiteral("exact")},
        {QStringLiteral("interface/accentColor"), QStringLiteral("#E0E0E0")},
        {QStringLiteral("interface/playlistMotion"), QStringLiteral("system")},
        {QStringLiteral("interface/osdHideMs"), 1400},
        {QStringLiteral("interface/cleanMediaNames"), true},
        {QStringLiteral("interface/fontEngine"), QString()},
        {QStringLiteral("advanced/demuxerMaxBytes"), QString()},
        {QStringLiteral("advanced/logLevel"), QStringLiteral("info")},
    };
    return values;
}

QVariant SettingsStore::defaultValue(const QString &key) const
{
    return defaults().value(key);
}

bool SettingsStore::contains(const QString &key) const
{
    QSettings settings = makeSettings();
    return settings.contains(QStringLiteral("settings/") + key);
}

QVariant SettingsStore::value(const QString &key, const QVariant &defaultValue) const
{
    QSettings settings = makeSettings();
    const QVariant fallback = defaults().value(key, defaultValue);
    QVariant result = settings.value(QStringLiteral("settings/") + key, fallback);
    if (!fallback.isValid())
        return result;

    // INI booleans arrive as strings; QML considers even "false" truthy.
    // Coerce known preferences here so all consumers see the same types.
    if (fallback.metaType().id() == QMetaType::Bool) {
        const QString text = result.toString().trimmed().toLower();
        if (text != "true" && text != "false" && text != "1" && text != "0")
            return fallback;
        return text == "true" || text == "1";
    }
    if (!result.convert(fallback.metaType()))
        return fallback;
    if (fallback.metaType().id() == QMetaType::Double && !std::isfinite(result.toDouble()))
        return fallback;
    // Preserve the earlier JPEG spelling without leaving the choice blank.
    if (key == "general/screenshotFormat" && result.toString() == "jpeg")
        return QStringLiteral("jpg");
    return result;
}

bool SettingsStore::flag(const QString &key, bool defaultValue) const
{
    return value(key, defaultValue).toBool();
}

void SettingsStore::setValue(const QString &key, const QVariant &value)
{
    QSettings settings = makeSettings();
    settings.setValue(QStringLiteral("settings/") + key, value);
    settings.sync();

    emit settingChanged(key, this->value(key));
}

void SettingsStore::resetSetting(const QString &key)
{
    QSettings settings = makeSettings();
    settings.remove(QStringLiteral("settings/") + key);
    settings.sync();

    emit settingChanged(key, value(key));
}

bool SettingsStore::isDefault(const QString &key) const
{
    const QVariant fallback = defaults().value(key);
    if (!fallback.isValid())
        return !contains(key);
    // value() already coerces the stored text to the default's type, so a
    // stored "true" and the bool default compare equal here.
    const QVariant current = value(key);
    if (fallback.metaType().id() == QMetaType::Double)
        return qFuzzyCompare(1.0 + current.toDouble(), 1.0 + fallback.toDouble());
    return current.toString() == fallback.toString();
}

void SettingsStore::resetAllSettings()
{
    QSettings settings = makeSettings();
    settings.beginGroup(QStringLiteral("settings"));
    QSet<QString> keys;
    const QStringList storedKeys = settings.allKeys();
    for (const QString &key : storedKeys)
        keys.insert(key);
    settings.endGroup();
    for (auto it = defaults().cbegin(); it != defaults().cend(); ++it)
        keys.insert(it.key());
    const bool hadShaders = !appliedShaders().isEmpty();
    settings.remove(QStringLiteral("settings"));
    settings.remove(QStringLiteral("shaders/applied"));
    settings.sync();

    // Stable order also makes reset transitions straightforward to inspect.
    QStringList ordered = keys.values();
    ordered.sort();
    for (const QString &key : ordered)
        emit settingChanged(key, defaultValue(key));
    if (hadShaders)
        emit appliedShadersChanged();
    emit settingsReset();
}

void SettingsStore::openLogFolder() const
{
    // Same path main.cpp's FileLogger resolves: AppDataLocation's parent
    // (org == app, so AppDataLocation already ends in Frame/Frame) + /logs.
    const QString dir = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/../logs"));
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QString SettingsStore::keyFor(const QString &filePath) const
{
    return QStringLiteral("resume/") + digestFor(filePath);
}

QString SettingsStore::digestFor(const QString &filePath) const
{
    const QByteArray digest =
        QCryptographicHash::hash(filePath.toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(digest.toHex());
}

// F6e. Stored as a string list of fixed-precision seconds: QSettings round
// trips those through the INI exactly, where a QVariantList of doubles comes
// back locale-formatted and lossy.
QVariantList SettingsStore::bookmarks(const QString &filePath) const
{
    const QString file = normalizePath(filePath);
    if (file.isEmpty())
        return {};

    QSettings settings = makeSettings();
    const QStringList raw =
        settings.value(QStringLiteral("bookmarks/") + digestFor(file)).toStringList();

    QList<double> marks;
    marks.reserve(raw.size());
    for (const QString &entry : raw) {
        bool ok = false;
        const double value = entry.toDouble(&ok);
        if (ok && std::isfinite(value) && value >= 0.0)
            marks.append(value);
    }
    std::sort(marks.begin(), marks.end());

    QVariantList result;
    result.reserve(marks.size());
    for (double mark : marks)
        result.append(mark);
    return result;
}

bool SettingsStore::toggleBookmark(const QString &filePath, double position)
{
    const QString file = normalizePath(filePath);
    if (file.isEmpty() || !std::isfinite(position) || position < 0.0)
        return false;

    // Half a second: close enough that a second press at the same spot reads
    // as "undo", far enough that deliberate nearby marks both survive.
    constexpr double kBookmarkEpsilon = 0.5;

    QList<double> marks;
    const QVariantList existing = bookmarks(file);
    marks.reserve(existing.size());
    for (const QVariant &entry : existing)
        marks.append(entry.toDouble());

    bool added = true;
    for (int i = 0; i < marks.size(); ++i) {
        if (std::fabs(marks.at(i) - position) <= kBookmarkEpsilon) {
            marks.removeAt(i);
            added = false;
            break;
        }
    }
    if (added)
        marks.append(position);
    std::sort(marks.begin(), marks.end());

    QStringList raw;
    raw.reserve(marks.size());
    for (double mark : marks)
        raw.append(QString::number(mark, 'f', 3));

    QSettings settings = makeSettings();
    const QString key = QStringLiteral("bookmarks/") + digestFor(file);
    if (raw.isEmpty())
        settings.remove(key);
    else
        settings.setValue(key, raw);
    settings.sync();
    return added;
}

void SettingsStore::clearBookmarks(const QString &filePath)
{
    const QString file = normalizePath(filePath);
    if (file.isEmpty())
        return;

    QSettings settings = makeSettings();
    settings.remove(QStringLiteral("bookmarks/") + digestFor(file));
    settings.sync();
}

QStringList SettingsStore::loadRecents() const
{
    QSettings settings = makeSettings();
    return settings.value(QStringLiteral("files/recent")).toStringList();
}

void SettingsStore::saveRecents(const QStringList &files)
{
    QSettings settings = makeSettings();
    settings.setValue(QStringLiteral("files/recent"), files);
    settings.sync();
}
