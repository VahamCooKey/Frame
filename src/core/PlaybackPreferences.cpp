#include "PlaybackPreferences.h"

#include "PlayerController.h"
#include "SettingsStore.h"

#include <QDir>
#include <QStringView>
#include <QStandardPaths>

namespace {

// mpv treats %% as a literal percent; a regex alone also matches %%n.
bool hasSequenceToken(QStringView text) {
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] != u'%') continue;
        if (++i >= text.size()) break;
        if (text[i] == u'%') continue;
        if (text[i] == u'#') ++i;
        while (i < text.size() && text[i] >= u'0' && text[i] <= u'9') ++i;
        if (i < text.size() && text[i] == u'n') return true;
        --i; // Another % can start immediately after an unrelated token.
    }
    return false;
}

struct Language {
    const char *label;
    const char *code;
    const char *aliases;
};

// Save language codes, never file-local numeric track IDs. Include common
// ISO-639 aliases because media tags do not consistently use two letters.
constexpr Language languages[] = {
    {"Arabic", "ar", "ar,ara"},
    {"Bengali", "bn", "bn,ben"},
    {"Chinese", "zh", "zh,zho,chi"},
    {"Czech", "cs", "cs,ces,cze"},
    {"Danish", "da", "da,dan"},
    {"Dutch", "nl", "nl,nld,dut"},
    {"English", "en", "en,eng"},
    {"Finnish", "fi", "fi,fin"},
    {"French", "fr", "fr,fra,fre"},
    {"German", "de", "de,deu,ger"},
    {"Greek", "el", "el,ell,gre"},
    {"Gujarati", "gu", "gu,guj"},
    {"Hebrew", "he", "he,heb"},
    {"Hindi", "hi", "hi,hin"},
    {"Hungarian", "hu", "hu,hun"},
    {"Indonesian", "id", "id,ind"},
    {"Italian", "it", "it,ita"},
    {"Japanese", "ja", "ja,jpn"},
    {"Kannada", "kn", "kn,kan"},
    {"Korean", "ko", "ko,kor"},
    {"Malay", "ms", "ms,msa,may"},
    {"Malayalam", "ml", "ml,mal"},
    {"Marathi", "mr", "mr,mar"},
    {"Norwegian", "no", "no,nor,nb,nob,nn,nno"},
    {"Persian", "fa", "fa,fas,per"},
    {"Polish", "pl", "pl,pol"},
    {"Portuguese", "pt", "pt,por"},
    {"Punjabi", "pa", "pa,pan"},
    {"Romanian", "ro", "ro,ron,rum"},
    {"Russian", "ru", "ru,rus"},
    {"Spanish", "es", "es,spa"},
    {"Swedish", "sv", "sv,swe"},
    {"Tamil", "ta", "ta,tam"},
    {"Telugu", "te", "te,tel"},
    {"Thai", "th", "th,tha"},
    {"Turkish", "tr", "tr,tur"},
    {"Ukrainian", "uk", "uk,ukr"},
    {"Urdu", "ur", "ur,urd"},
    {"Vietnamese", "vi", "vi,vie"},
};

const Language *languageFor(const QString &raw)
{
    const QString code = raw.trimmed().toLower();
    if (code.isEmpty()) return nullptr;
    for (const auto &language : languages) {
        if (QString::fromLatin1(language.aliases).split(QLatin1Char(',')).contains(code))
            return &language;
    }
    return nullptr;
}

QString canonicalLanguage(const QString &raw)
{
    const auto *language = languageFor(raw);
    return language ? QString::fromLatin1(language->code) : QString();
}

QString languageList(const QString &raw)
{
    const auto *language = languageFor(raw);
    return language ? QString::fromLatin1(language->aliases) : QString();
}

QString optionString(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
    if (value.metaType().id() == QMetaType::Double)
        return QString::number(value.toDouble(), 'g', 15);
    if (value.metaType().id() == QMetaType::QVariantList || value.metaType().id() == QMetaType::QStringList) {
        QStringList strings;
        for (const auto &item : value.toList()) strings.append(item.toString());
        return strings.join(QLatin1Char(','));
    }
    return value.toString();
}

const QStringList &aspects()
{
    static const QStringList values{QStringLiteral("no"), QStringLiteral("16:9"),
                                    QStringLiteral("4:3"), QStringLiteral("2.35")};
    return values;
}

struct Option {
    const char *key;
    const char *property;
};

constexpr Option directOptions[] = {
    {"general/screenshotFormat", "screenshot-format"},
    {"general/screenshotDirectory", "screenshot-directory"},
    {"general/screenshotTemplate", "screenshot-template"},
    {"video/deinterlace", "deinterlace"},
    {"video/videoSync", "video-sync"},
    {"audio/device", "audio-device"},
    {"audio/delayDefault", "audio-delay"},
    {"subtitles/fontSize", "sub-font-size"},
    {"subtitles/color", "sub-color"},
    {"subtitles/borderSize", "sub-border-size"},
    {"subtitles/position", "sub-pos"},
    {"subtitles/delayDefault", "sub-delay"},
    {"subtitles/autoLoad", "sub-auto"},
};

} // namespace

PlaybackPreferences::PlaybackPreferences(SettingsStore *settings, PlayerController *player, QObject *parent)
    : QObject(parent), m_settings(settings), m_player(player),
      m_initialCacheBytes(player->mpvProperty(QStringLiteral("demuxer-max-bytes"))),
      m_initialHwdec(player->mpvProperty(QStringLiteral("hwdec")))
{
    connect(settings, &SettingsStore::settingChanged, this, [this](const QString &key) {
        applySetting(key);
        if (key == "video/aspectOverride" || key == "audio/delayDefault" || key == "subtitles/delayDefault")
            emit fileDefaultsChanged();
        if (key == "audio/preferredLanguage" || key == "subtitles/preferredLanguage")
            emit languagePreferencesChanged();
        if (key == "video/shadersEnabled")
            emit shadersChanged();
    });
    connect(settings, &SettingsStore::appliedShadersChanged, this, [this] {
        applyShaders();
        emit shadersChanged();
    });
    applyAll();
}

QString PlaybackPreferences::defaultAspect() const
{
    const QString saved = m_settings->value(QStringLiteral("video/aspectOverride")).toString();
    return aspects().contains(saved) ? saved : QStringLiteral("no");
}

int PlaybackPreferences::defaultAspectIndex() const
{
    return static_cast<int>(aspects().indexOf(defaultAspect()));
}

QVariantMap PlaybackPreferences::fileDefaults() const
{
    return {{QStringLiteral("video-aspect-override"), defaultAspect()},
            {QStringLiteral("audio-delay"), m_settings->value(QStringLiteral("audio/delayDefault"))},
            {QStringLiteral("sub-delay"), m_settings->value(QStringLiteral("subtitles/delayDefault"))}};
}

QVariantList PlaybackPreferences::languageOptions() const
{
    QVariantList result{QVariantMap{{QStringLiteral("label"), QStringLiteral("Automatic")},
                                   {QStringLiteral("value"), QString()}}};
    for (const auto &language : languages)
        result.append(QVariantMap{{QStringLiteral("label"), QString::fromLatin1(language.label)},
                                  {QStringLiteral("value"), QString::fromLatin1(language.code)}});
    return result;
}

QString PlaybackPreferences::preferredAudioLanguage() const
{
    return canonicalLanguage(m_settings->value(QStringLiteral("audio/preferredLanguage")).toString());
}

QString PlaybackPreferences::preferredSubtitleLanguage() const
{
    return canonicalLanguage(m_settings->value(QStringLiteral("subtitles/preferredLanguage")).toString());
}

void PlaybackPreferences::setPreferredAudioLanguage(const QString &language)
{
    m_settings->setValue(QStringLiteral("audio/preferredLanguage"), canonicalLanguage(language));
}

void PlaybackPreferences::setPreferredSubtitleLanguage(const QString &language)
{
    m_settings->setValue(QStringLiteral("subtitles/preferredLanguage"), canonicalLanguage(language));
}

QStringList PlaybackPreferences::shaders() const
{
    return m_settings->appliedShaders();
}

bool PlaybackPreferences::shadersEnabled() const
{
    return m_settings->flag(QStringLiteral("video/shadersEnabled"));
}

void PlaybackPreferences::setShadersEnabled(bool enabled)
{
    m_settings->setValue(QStringLiteral("video/shadersEnabled"), enabled);
}

void PlaybackPreferences::removeShader(const QString &path)
{
    auto remaining = shaders();
    remaining.removeAll(QDir::fromNativeSeparators(path));
    m_settings->setAppliedShaders(remaining);
}

void PlaybackPreferences::resetShaders()
{
    m_settings->setAppliedShaders({});
    setShadersEnabled(true);
}

void PlaybackPreferences::setOption(const QString &name, const QVariant &value)
{
    m_player->command({QStringLiteral("set"), name, optionString(value)});
}

void PlaybackPreferences::applyShaders()
{
    // Clear and append separate arguments: shader paths containing spaces,
    // colons or list separators must never be serialized into a path list.
    m_player->command({QStringLiteral("change-list"), QStringLiteral("glsl-shaders"),
                       QStringLiteral("clr"), QString()});
    if (shadersEnabled()) {
        for (const QString &path : shaders())
            m_player->loadShader(path);
    }
}

QString PlaybackPreferences::defaultScreenshotDirectoryPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (base.isEmpty())
        base = QDir::currentPath();
    return QDir::toNativeSeparators(QDir::cleanPath(base + QStringLiteral("/Frame")));
}

void PlaybackPreferences::applySetting(const QString &key)
{
    if (key == "general/screenshotTemplate") {
        // mpv refuses to overwrite ("Can't save screenshot, file ... already
        // exists!") and only its %n token makes a name unique, so a second
        // shot of the same second, or of a paused frame, failed outright.
        // Templates without a sequence token get a two-digit "lowest free
        // number" counter: Frame_clip_00_01_05_00.png, then _01.
        QString value = m_settings->value(key).toString();
        if (value.isEmpty())
            value = SettingsStore::defaults().value(key).toString();
        if (!hasSequenceToken(QStringView(value)))
            value += QStringLiteral("_%#02n");
        setOption(QStringLiteral("screenshot-template"), value);
        return;
    }
    if (key == "general/screenshotDirectory") {
        // An empty setting means "the default", not "wherever the process
        // happens to be running". mpv creates the folder on first use.
        const QString chosen = m_settings->value(key).toString();
        setOption(QStringLiteral("screenshot-directory"),
                  chosen.isEmpty() ? defaultScreenshotDirectoryPath() : chosen);
        return;
    }
    for (const auto &option : directOptions) {
        if (key == QLatin1String(option.key)) {
            setOption(QLatin1String(option.property), m_settings->value(key));
            return;
        }
    }
    if (key == "video/aspectOverride")
        setOption(QStringLiteral("video-aspect-override"), defaultAspect());
    else if (key == "video/hwdec") {
        const QVariant value = m_settings->contains(key) ? m_settings->value(key) : m_initialHwdec;
        if (value.isValid()) setOption(QStringLiteral("hwdec"), value);
    } else if (key == "advanced/demuxerMaxBytes") {
        const QString saved = m_settings->value(key).toString();
        const QVariant value = saved.isEmpty() ? m_initialCacheBytes : QVariant(saved);
        if (value.isValid()) setOption(QStringLiteral("demuxer-max-bytes"), value);
    } else if (key == "general/playlistRepeat")
        setOption(QStringLiteral("loop-playlist"), m_settings->flag(key) ? QStringLiteral("inf") : QStringLiteral("no"));
    else if (key == "audio/normalize")
        setOption(QStringLiteral("af"), m_settings->flag(key) ? QStringLiteral("dynaudnorm") : QString());
    else if (key == "subtitles/autoSelect")
        setOption(QStringLiteral("subs-fallback"), m_settings->flag(key) ? QStringLiteral("yes") : QStringLiteral("default"));
    else if (key == "advanced/logLevel")
        setOption(QStringLiteral("msg-level"), QStringLiteral("all=") + m_settings->value(key).toString());
    else if (key == "audio/preferredLanguage" || key == "subtitles/preferredLanguage") {
        const QString name = key == "audio/preferredLanguage" ? QStringLiteral("alang") : QStringLiteral("slang");
        const QString language = key == "audio/preferredLanguage" ? preferredAudioLanguage() : preferredSubtitleLanguage();
        if (language.isEmpty()) {
            // set name "" creates a one-item list containing an empty string.
            // clr restores the actual empty Automatic language list.
            m_player->command({QStringLiteral("change-list"), name, QStringLiteral("clr"), QString()});
        } else {
            setOption(name, languageList(language));
        }
    }
    else if (key == "video/shadersEnabled")
        applyShaders();
}

void PlaybackPreferences::applyAll()
{
    const auto &defaults = SettingsStore::defaults();
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
        applySetting(it.key());
}
