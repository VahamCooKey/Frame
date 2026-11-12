// Not a CONTRACT header (new, single-owner): filename-only "Netflix-style"
// display name cleanup (improvement-plan.md S7.5). Presentation layer only --
// the cleaned string must never be written back into SettingsStore keys,
// resume/bookmark hashes, or anything handed to mpv; those all key on the
// real path. No network, ever (S7.6's online lookup is explicitly deferred).
#pragma once
#include <QObject>
#include <QString>

class MediaNameFormatter : public QObject {
    Q_OBJECT
    // Read from Settings once at startup in main.cpp; every settings pane
    // that offers the toggle (currently just Interface) sets this directly
    // on change, the same live-set pattern Colors.accent uses -- a
    // Settings.value() binding does not auto-update, only a NOTIFYable
    // property does. All five display call sites read this to decide
    // cleaned() vs baseName().
    Q_PROPERTY(bool cleanupEnabled READ cleanupEnabled WRITE setCleanupEnabled NOTIFY cleanupEnabledChanged)

public:
    explicit MediaNameFormatter(QObject *parent = nullptr);

    bool cleanupEnabled() const { return m_cleanupEnabled; }
    void setCleanupEnabled(bool enabled);

    struct Parsed {
        QString show;
        QString season;        // "01", empty when not an episode
        QString episode;       // "02", empty when not an episode
        QString episodeEnd;    // second episode of "S01E02E03", else empty
        QString episodeTitle;  // "The Reckoning" -- often empty, that's normal
        QString year;          // "2019", empty when not a dated movie
        QString raw;           // filename with extension, unmodified
    };

    // Pure, offline, deterministic: same path always yields the same result,
    // no I/O beyond the string itself. filePath may be a bare title too (a
    // playlist entry's mpv-reported title, say) -- only its last path
    // segment is used and a recognized media extension is stripped from it.
    static Parsed parse(const QString &filePath);

    // parse() rendered into a single display line ("Show Name  ·  S01E02
    // ·  The Reckoning", "Movie Name (2019)"), or the plain filename
    // (extension stripped, separators left alone) when no rule matched.
    Q_INVOKABLE static QString cleaned(const QString &filePath);

    // What every QML call site actually wants: cleaned() when cleanupEnabled,
    // else the raw filename (with extension) -- the pre-S7.5 behaviour.
    Q_INVOKABLE static QString display(const QString &filePath, bool cleanupEnabled);

    // Same as display(), but reads the live cleanupEnabled property instead
    // of taking it as an argument -- what every QML call site actually
    // calls, so the per-session "Aa" toggle and the persisted setting both
    // reach every site through one property instead of five bindings.
    Q_INVOKABLE QString displayFor(const QString &filePath) const;

    // mpv's media-title is not a substitute for the parser (S7.5.2): it is
    // real container metadata when present, and just the bare filename
    // otherwise -- inconsistent by nature, since mpv itself can't tell the
    // caller which case it is. Precedence: prefer mediaTitle when it looks
    // like genuine metadata (differs from the filename once the extension
    // is stripped); otherwise it's mpv's own filename fallback, so parse
    // filePath instead. Used by the window title, title bar and the OSD
    // audio-only label -- every site that has both an mpv title and a path.
    Q_INVOKABLE QString titleFor(const QString &mediaTitle, const QString &filePath) const;

    // filename.ext with no directory -- shared with PlaylistDrawer's own
    // slash-splitting so both stay in agreement about what "raw" means.
    Q_INVOKABLE static QString baseName(const QString &filePath);

signals:
    void cleanupEnabledChanged();

private:
    bool m_cleanupEnabled = true;
};
