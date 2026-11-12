#include "core/TrackPreference.h"

#include <QLocale>
#include <QRegularExpression>

namespace {

QString normalizedLanguage(QString language)
{
    language = language.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
    const QString primary = language.section(QLatin1Char('-'), 0, 0);
    if (primary.isEmpty() || primary == "und" || primary == "unknown") return {};
    const auto known = QLocale::codeToLanguage(primary);
    if (known != QLocale::C && known != QLocale::AnyLanguage) {
        const auto canonical = QLocale::languageToCode(known);
        if (!canonical.isEmpty()) language.replace(0, primary.size(), canonical);
    }
    return language;
}

QVariantMap identity(const QVariantMap &track)
{
    const QString title = track.value("title").toString().simplified().toCaseFolded();
    static const QRegularExpression commentary(QStringLiteral("\\bcommentary\\b"));
    static const QRegularExpression described(QStringLiteral("\\b(audio description|descriptive audio|described)\\b"));
    static const QRegularExpression captions(QStringLiteral("\\b(sdh|cc|hearing[ -]impaired)\\b"));
    static const QRegularExpression forced(QStringLiteral("\\bforced\\b"));
    static const QRegularExpression signs(QStringLiteral("\\b(signs|songs)\\b"));
    return {
        {"lang", normalizedLanguage(track.value("lang").toString())},
        {"title", title},
        {"forced", track.value("forced").toBool() || forced.match(title).hasMatch()},
        {"hearing", track.value("hearing-impaired").toBool() || captions.match(title).hasMatch()},
        {"visual", track.value("visual-impaired").toBool() || described.match(title).hasMatch()},
        {"commentary", track.value("commentary").toBool() || commentary.match(title).hasMatch()},
        {"signs", track.value("type") == "sub" && signs.match(title).hasMatch()},
        {"codec", track.value("codec").toString().toLower()},
        {"channels", track.value("demux-channel-count").toInt()},
        {"external", track.value("external").toBool()},
    };
}

QVariantList tracksOfType(const QVariantList &tracks, const QString &type)
{
    QVariantList result;
    for (const auto &entry : tracks)
        if (entry.toMap().value("type") == type) result.append(entry);
    return result;
}

QVariantList layout(const QVariantList &tracks)
{
    QVariantList result;
    for (const auto &entry : tracks) result.append(identity(entry.toMap()));
    return result;
}

} // namespace

bool TrackPreference::remember(const QVariantList &tracks, const QString &type, int id)
{
    if (id < 0) {
        m_mode = Mode::Off;
        m_identity.clear();
        m_layout.clear();
        m_ordinal = -1;
        return true;
    }
    const auto candidates = tracksOfType(tracks, type);
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        if (candidates[i].toMap().value("id").toInt() != id) continue;
        m_mode = Mode::Selected;
        m_identity = identity(candidates[i].toMap());
        m_layout = layout(candidates);
        m_ordinal = static_cast<int>(i);
        return true;
    }
    return false; // An invalid/stale menu choice must not erase a good preference.
}

int TrackPreference::matchingId(const QVariantList &tracks, const QString &type) const
{
    if (m_mode != Mode::Selected) return -1;
    const auto candidates = tracksOfType(tracks, type);
    const auto identities = layout(candidates);
    // Untagged tracks can only be carried over when the complete ordered
    // layout agrees. Never reuse a raw track ID in an unrelated layout.
    if (identities == m_layout && m_ordinal >= 0 && m_ordinal < candidates.size())
        return candidates[m_ordinal].toMap().value("id").toInt();

    const auto language = m_identity.value("lang").toString();
    const auto title = m_identity.value("title").toString();
    if (language.isEmpty() && title.isEmpty()) return -1;

    int bestId = -1, bestScore = -1;
    bool tied = false;
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        const auto candidate = candidates[i].toMap();
        const auto candidateIdentity = identities[i].toMap();
        const auto candidateLanguage = candidateIdentity.value("lang").toString();
        const bool sameTitle = !title.isEmpty() && candidateIdentity.value("title") == title;
        if (!language.isEmpty()) {
            if (language.section('-', 0, 0) != candidateLanguage.section('-', 0, 0)) continue;
        } else if (!sameTitle) {
            continue;
        }
        bool sameRole = true;
        for (const auto *role : {"forced", "hearing", "visual", "commentary", "signs"})
            if (m_identity.value(role) != candidateIdentity.value(role)) sameRole = false;
        if (!sameRole) continue;

        int score = sameTitle ? 256 : 0;
        if (candidateLanguage == language) score += 512;
        if (candidateIdentity.value("channels") == m_identity.value("channels")) score += 8;
        if (candidateIdentity.value("codec") == m_identity.value("codec")) score += 4;
        if (candidateIdentity.value("external") == m_identity.value("external")) score += 1;
        if (score > bestScore) {
            bestId = candidate.value("id").toInt();
            bestScore = score;
            tied = false;
        } else if (score == bestScore) {
            tied = true;
        }
    }
    return tied ? -1 : bestId;
}
