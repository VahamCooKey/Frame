#include "MediaNameFormatter.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

// Same extension list as PlayerController::siblingMediaFiles() (F16); kept
// in sync by hand since duplicating five constants is cheaper than a shared
// header for one string list.
bool isMediaExtension(const QString &ext)
{
    static const QStringList kExts = {
        QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("m4v"),
        QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("webm"),
        QStringLiteral("wmv"), QStringLiteral("flv"), QStringLiteral("ts"),
        QStringLiteral("m2ts"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
        QStringLiteral("ogv"), QStringLiteral("3gp"), QStringLiteral("mp3"),
        QStringLiteral("m4a"), QStringLiteral("flac"), QStringLiteral("wav"),
        QStringLiteral("aac"), QStringLiteral("ogg"), QStringLiteral("opus"),
    };
    return kExts.contains(ext, Qt::CaseInsensitive);
}

QString stripExtension(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && isMediaExtension(name.mid(dot + 1)))
        return name.left(dot);
    return name;
}

// Rule 4 (tag stripping) + rule 6 (bracket handling) + rule 5 (separator
// normalization), applied to one fragment (a show/title/episode-title
// candidate). Idempotent: running it twice gives the same result, which is
// what keeps parse() safe to fall back to it after a rejected match.
QString cleanupFragment(QString text)
{
    // Rule 6: drop whole bracketed groups first ("[1080p]", "[Group]",
    // "(x264)") -- generic release-tag clusters, including the anime
    // fansub "[hash]" suffix. A parenthesized year is never in here: the
    // movie-year rule extracts the year from the full name before this
    // function ever sees the title fragment.
    static const QRegularExpression kBracketGroup(
        QStringLiteral("[\\[(][^\\[\\]()]{1,40}[\\])]"));
    text.remove(kBracketGroup);
    // Any lone unmatched bracket char left over (e.g. from truncating the
    // fragment right at a "(" before a year) is just noise.
    static const QRegularExpression kStrayBracket(QStringLiteral("[\\[\\]()]"));
    text.remove(kStrayBracket);

    // Rule 4: resolution / source / codec / audio tags, as whole tokens
    // bounded by a separator or the fragment edge.
    static const QStringList kTags = {
        QStringLiteral("2160p"), QStringLiteral("1080p"), QStringLiteral("720p"),
        QStringLiteral("480p"), QStringLiteral("4K"), QStringLiteral("UHD"),
        QStringLiteral("WEB-DL"), QStringLiteral("WEBDL"), QStringLiteral("WEBRip"),
        QStringLiteral("WEB"), QStringLiteral("BluRay"), QStringLiteral("BDRip"),
        QStringLiteral("BRRip"), QStringLiteral("HDTV"), QStringLiteral("DVDRip"),
        QStringLiteral("REMUX"), QStringLiteral("x264"), QStringLiteral("x265"),
        QStringLiteral("H.264"), QStringLiteral("H264"), QStringLiteral("HEVC"),
        QStringLiteral("AV1"), QStringLiteral("XviD"), QStringLiteral("AAC"),
        QStringLiteral("DDP5.1"), QStringLiteral("DDP"), QStringLiteral("AC3"),
        QStringLiteral("DTS"), QStringLiteral("Atmos"), QStringLiteral("TrueHD"),
    };
    for (const QString &tag : kTags) {
        const QRegularExpression re(
            QStringLiteral("(?i)(?:^|[.\\-_ ])") + QRegularExpression::escape(tag)
            + QStringLiteral("(?:$|[.\\-_ ])"));
        text.replace(re, QStringLiteral(" "));
    }

    // Trailing "-GROUP" release tag.
    static const QRegularExpression kGroupSuffix(QStringLiteral("-[A-Za-z0-9]{2,15}$"));
    text.remove(kGroupSuffix);

    // Rule 5: dots/underscores -> spaces, but ONLY when the fragment has no
    // real spaces already -- a legitimately-spaced name ("Mr. Robot") must
    // not have its dots mangled.
    if (!text.contains(QLatin1Char(' '))) {
        text.replace(QLatin1Char('.'), QLatin1Char(' '));
        text.replace(QLatin1Char('_'), QLatin1Char(' '));
    }

    text = text.simplified();
    while (!text.isEmpty() && (text.front() == QLatin1Char('-') || text.front() == QLatin1Char('.')))
        text.remove(0, 1);
    while (!text.isEmpty() && (text.back() == QLatin1Char('-') || text.back() == QLatin1Char('.')))
        text.chop(1);
    return text.trimmed();
}

// Rule 3: text between the episode marker and the first quality/source/
// codec tag, when present. Absence is normal, not an error -- most
// releases have no episode-title segment at all.
QString extractEpisodeTitle(const QString &tail)
{
    static const QRegularExpression kFirstTag(QStringLiteral(
        "(?i)[.\\-_ ](?:2160p|1080p|720p|480p|4K|UHD|WEB-DL|WEBDL|WEBRip|WEB|"
        "BluRay|BDRip|BRRip|HDTV|DVDRip|REMUX|x264|x265|H\\.264|H264|HEVC|AV1|"
        "XviD|AAC|DDP|AC3|DTS|Atmos|TrueHD)(?:[.\\-_ ]|$)"));
    const auto tagMatch = kFirstTag.match(tail);
    const QString title = tagMatch.hasMatch() ? tail.left(tagMatch.capturedStart()) : tail;
    return cleanupFragment(title);
}

} // namespace

MediaNameFormatter::MediaNameFormatter(QObject *parent)
    : QObject(parent)
{
}

void MediaNameFormatter::setCleanupEnabled(bool enabled)
{
    if (m_cleanupEnabled == enabled)
        return;
    m_cleanupEnabled = enabled;
    emit cleanupEnabledChanged();
}

QString MediaNameFormatter::baseName(const QString &filePath)
{
    if (filePath.isEmpty())
        return QString();
    const int slash = qMax(filePath.lastIndexOf(QLatin1Char('/')),
                            filePath.lastIndexOf(QLatin1Char('\\')));
    return slash >= 0 ? filePath.mid(slash + 1) : filePath;
}

MediaNameFormatter::Parsed MediaNameFormatter::parse(const QString &filePath)
{
    Parsed result;
    const QString raw = baseName(filePath);
    result.raw = raw;
    if (raw.trimmed().isEmpty())
        return result;

    QString name = stripExtension(raw);

    // Anime fansub group prefix: "[Group] Show Name - 12 [1080p][hash]"
    // puts the group first. Strip it before anything else so the generic
    // bracket-stripping in cleanupFragment() never gets a chance to eat
    // the show name that follows it.
    static const QRegularExpression kGroupPrefix(QStringLiteral("^\\s*\\[[^\\]]{1,40}\\]\\s*"));
    const auto groupMatch = kGroupPrefix.match(name);
    if (groupMatch.hasMatch())
        name = name.mid(groupMatch.capturedLength(0));

    // 1. Episode form: S01E02, 1x02, S01E02E03 (multi-episode), "Part 2",
    // and the anime fansub convention "Show Name - 12" (a bare episode
    // number after a space-dash-space, no season). The last alternative
    // requires whitespace on both sides of the dash so it can't fire on a
    // hyphenated show name ("9-1-1") or a trailing "-GROUP" release tag,
    // neither of which has surrounding spaces.
    // Tried before the movie-year rule so "Show.Name.2019.S01E02" reads as
    // a series (year is incidental), not a 2019 movie.
    static const QRegularExpression kEpisode(QStringLiteral(
        "(?i)(?:S(?<s1>\\d{1,2})E(?<e1>\\d{1,3})(?:E(?<e2>\\d{1,3}))?"
        "|(?<s2>\\d{1,2})x(?<e3>\\d{1,3})"
        "|Part[. _-]?(?<e4>\\d{1,2})"
        "|(?:^|\\s)-\\s(?<e5>\\d{1,3})(?=\\s|$))"));
    const auto epMatch = kEpisode.match(name);
    if (epMatch.hasMatch()) {
        const QString show = cleanupFragment(name.left(epMatch.capturedStart(0)));
        if (show.size() >= 2) {
            result.show = show;
            if (!epMatch.captured(QStringLiteral("s1")).isEmpty()) {
                result.season = epMatch.captured(QStringLiteral("s1")).rightJustified(2, QLatin1Char('0'));
                result.episode = epMatch.captured(QStringLiteral("e1")).rightJustified(2, QLatin1Char('0'));
                if (!epMatch.captured(QStringLiteral("e2")).isEmpty())
                    result.episodeEnd = epMatch.captured(QStringLiteral("e2")).rightJustified(2, QLatin1Char('0'));
            } else if (!epMatch.captured(QStringLiteral("s2")).isEmpty()) {
                result.season = epMatch.captured(QStringLiteral("s2")).rightJustified(2, QLatin1Char('0'));
                result.episode = epMatch.captured(QStringLiteral("e3")).rightJustified(2, QLatin1Char('0'));
            } else if (!epMatch.captured(QStringLiteral("e4")).isEmpty()) {
                result.episode = epMatch.captured(QStringLiteral("e4"));
            } else {
                result.episode = epMatch.captured(QStringLiteral("e5"));
            }
            result.episodeTitle = extractEpisodeTitle(name.mid(epMatch.capturedEnd(0)));
            return result;
        }
        // Implausibly short show name (e.g. the marker was at the very
        // start) -- fall through to the safety net at the bottom.
    }

    // 2. Movie form: a 4-digit year (1900-2099) acts as the terminator.
    // Daily shows ("Show.Name.2024.03.15") are guarded against: a year
    // immediately followed by ".MM.DD" is a date, not a release year.
    static const QRegularExpression kYear(QStringLiteral("(?<![0-9])(19\\d{2}|20\\d{2})(?![0-9])"));
    static const QRegularExpression kDateTail(QStringLiteral("^[.\\-_](\\d{2})[.\\-_](\\d{2})(?!\\d)"));
    QRegularExpressionMatch lastYear;
    auto yearIt = kYear.globalMatch(name);
    while (yearIt.hasNext()) {
        const auto m = yearIt.next();
        if (kDateTail.match(name.mid(m.capturedEnd())).hasMatch())
            continue;
        lastYear = m;
    }
    if (lastYear.hasMatch()) {
        const QString title = cleanupFragment(name.left(lastYear.capturedStart()));
        if (title.size() >= 2) {
            result.show = title;
            result.year = lastYear.captured();
            return result;
        }
    }

    // 3. Safety net: no rule matched, or a match yielded an implausibly
    // short/empty name. Fall back to the tag-stripped, separator-normalized
    // filename -- a cleanup that hides the file is worse than no cleanup.
    QString fallback = cleanupFragment(name);
    if (fallback.size() < 2)
        fallback = name;
    result.show = fallback;
    return result;
}

QString MediaNameFormatter::cleaned(const QString &filePath)
{
    const Parsed p = parse(filePath);
    if (p.show.isEmpty())
        return stripExtension(baseName(filePath));

    // U+00B7 MIDDLE DOT / U+2013 EN DASH, built from codepoints rather than
    // embedded as literal UTF-8 source bytes so MSVC's source-charset
    // detection (no BOM on this file) can't mangle them.
    const QString kDotSeparator = QLatin1String("  ") + QChar(0x00B7) + QLatin1String("  ");
    const QString kEnDash = QString(QChar(0x2013));

    if (!p.season.isEmpty() || !p.episode.isEmpty()) {
        QString line = p.show;
        if (!p.season.isEmpty()) {
            line += kDotSeparator + QStringLiteral("S") + p.season + QLatin1Char('E') + p.episode;
            if (!p.episodeEnd.isEmpty())
                line += kEnDash + QStringLiteral("E") + p.episodeEnd;
        } else {
            line += kDotSeparator + QStringLiteral("Part ") + p.episode;
        }
        if (!p.episodeTitle.isEmpty())
            line += kDotSeparator + p.episodeTitle;
        return line;
    }

    if (!p.year.isEmpty())
        return p.show + QStringLiteral(" (") + p.year + QLatin1Char(')');

    return p.show;
}

QString MediaNameFormatter::display(const QString &filePath, bool cleanupEnabled)
{
    return cleanupEnabled ? cleaned(filePath) : baseName(filePath);
}

QString MediaNameFormatter::displayFor(const QString &filePath) const
{
    return display(filePath, m_cleanupEnabled);
}

QString MediaNameFormatter::titleFor(const QString &mediaTitle, const QString &filePath) const
{
    if (mediaTitle.isEmpty())
        return QString();

    // mpv's fallback is the plain filename WITH extension (verified against
    // a running instance -- the mpv manual's "the filename if nothing else
    // is available" undersells that it keeps the extension). Comparing
    // against baseName() rather than the stripped name is what lets a real
    // title that happens to equal the filename ("Interview with a
    // friend.mkv" as both title and filename) still be trusted as metadata
    // rather than mis-detected as mpv's fallback.
    if (mediaTitle == baseName(filePath))
        return displayFor(filePath);

    return mediaTitle;
}
