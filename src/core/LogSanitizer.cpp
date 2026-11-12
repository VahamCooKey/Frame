#include "core/LogSanitizer.h"

#include <QRegularExpression>
#include <QUrl>

namespace {

QString redactUrls(const QString &message)
{
    // Do not guess which path segment or query key carries a credential.
    // Keep only the origin of network URLs. The two-character minimum on
    // schemes avoids mistaking a Windows drive path for a URL.
    static const QRegularExpression urls(
        QStringLiteral("\\b([a-z][a-z0-9+.-]+)://[^\\s<>\"']+"),
        QRegularExpression::CaseInsensitiveOption);
    QString result;
    result.reserve(message.size());
    qsizetype cursor = 0;
    auto matches = urls.globalMatch(message);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString raw = match.captured();
        QString token = raw;
        // Keep surrounding prose punctuation outside the replacement. A URL
        // containing these characters at its end is still fully redacted.
        while (!token.isEmpty() && QStringView(u".,;:!?)").contains(token.back()))
            token.chop(1);
        const QUrl url(token, QUrl::TolerantMode);
        QString replacement;
        if (url.isValid() && url.scheme() == QStringLiteral("file")
            && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment()) {
            replacement = token; // Ordinary local paths remain diagnostic.
        } else if (!url.isValid() || url.host().isEmpty()) {
            // Malformed URLs can still contain secrets. Never fall back to
            // logging the original token because structured parsing failed.
            replacement = match.captured(1).toLower() + QStringLiteral("://[redacted]");
        } else {
            replacement = url.adjusted(QUrl::RemoveUserInfo | QUrl::RemovePath
                | QUrl::RemoveQuery | QUrl::RemoveFragment).toString(QUrl::FullyEncoded);
            if (!url.userInfo().isEmpty() || !url.path().isEmpty() || url.hasQuery() || url.hasFragment())
                replacement += QStringLiteral("/[redacted]");
        }
        result += QStringView(message).mid(cursor, match.capturedStart() - cursor);
        result += replacement;
        result += QStringView(raw).mid(token.size());
        cursor = match.capturedEnd();
    }
    result += QStringView(message).mid(cursor);
    return result;
}

QString redactFields(const QString &message)
{
    // Cover common non-URL option/JSON shapes as well. Quoted values may
    // contain whitespace or escaped quotes, which the old regex missed.
    static const QRegularExpression fields(
        QStringLiteral("((?:api[_-]?key|access[_-]?token|auth[_-]?token|token|password|passwd|"
                       "client[_-]?secret|secret|credential(?:s)?|signature|sig|"
                       "x-emby-token|x-plex-token|session[_-]?id)[\"']?[ \\t]*[=:][ \\t]*)"),
        QRegularExpression::CaseInsensitiveOption);
    QString result;
    result.reserve(message.size());
    qsizetype cursor = 0;
    auto matches = fields.globalMatch(message);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedStart() < cursor) continue; // Inside an already hidden value.
        const qsizetype start = match.capturedEnd();
        qsizetype end = start;
        QString replacement = QStringLiteral("[redacted]");
        if (start < message.size() && (message[start] == u'\'' || message[start] == u'"')) {
            const QChar quote = message[start];
            end = start + 1;
            while (end < message.size()) {
                if (message[end] == u'\\' && end + 1 < message.size()) { end += 2; continue; }
                if (message[end] == quote) break;
                ++end;
            }
            replacement.prepend(quote);
            if (end < message.size()) {
                replacement.append(quote);
                ++end;
            }
        } else if (QStringView(message).mid(start).startsWith(QStringView(u"[redacted]"))) {
            end += 10; // Keep sanitization idempotent, including option delimiters.
        } else if (start < message.size() && (message[start] == u'[' || message[start] == u'{')) {
            // An unexpected structured credential is safer to omit with the
            // rest of its line than to partially reveal nested values.
            while (end < message.size() && message[end] != u'\r' && message[end] != u'\n') ++end;
        } else {
            while (end < message.size() && !message[end].isSpace()
                   && !QStringView(u"&,;'\"").contains(message[end])) ++end;
        }
        if (end == start) continue;
        result += QStringView(message).mid(cursor, start - cursor);
        result += replacement;
        cursor = end;
    }
    result += QStringView(message).mid(cursor);
    return result;
}

} // namespace

QString LogSanitizer::sanitize(QString message)
{
    message = redactUrls(message);
    // Header values can contain several cookies, quoted pieces, commas and
    // folded continuations. Hide the whole value, not just its first token.
    // For inline/JSON header dumps this deliberately hides the line's tail.
    static const QRegularExpression headers(
        QStringLiteral("(\\b(?:(?:proxy-)?authorization|(?:set-)?cookie|(?:content-)?location|referer|referrer)"
                       "[\"']?[ \\t]*[:=][ \\t]*)[^\\r\\n]*(?:\\r?\\n[ \\t]+[^\\r\\n]*)*"),
        QRegularExpression::CaseInsensitiveOption);
    message.replace(headers, QStringLiteral("\\1[redacted]"));
    // Verbose HTTP logs may print only a relative signed request target.
    static const QRegularExpression requestTarget(
        QStringLiteral("(\\b(?:GET|HEAD|POST|PUT|PATCH|DELETE|OPTIONS|TRACE|CONNECT)[ \\t]+)"
                       "[^\\r\\n]+?([ \\t]+HTTP/\\d(?:\\.\\d)?\\b)"),
        QRegularExpression::CaseInsensitiveOption);
    message.replace(requestTarget, QStringLiteral("\\1[redacted]\\2"));
    message = redactFields(message);
    static const QRegularExpression bearer(
        QStringLiteral("(\\bBearer[ \\t]+)[^\\s,;'\"\\\\]+"),
        QRegularExpression::CaseInsensitiveOption);
    message.replace(bearer, QStringLiteral("\\1[redacted]"));
    return message;
}
