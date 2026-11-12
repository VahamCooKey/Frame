#include <QtTest>

#include "core/MediaNameFormatter.h"

class TestMediaNameFormatter : public QObject {
    Q_OBJECT

private slots:
    void episodeWithTitle();
    void episodeNoTitle();
    void movieWithYear();
    void animeFansubOrder();
    void numericShowName();
    void yearInShowName();
    void dailyShowDate();
    void resolutionVsEpisodeCode();
    void nonLatinPassThrough();
    void alreadyCleanIsIdempotent();
    void multiEpisode();
    void emptyAndBareExtension();
    void spacedNameDotsUntouched();
    void displayTogglesCleanup();
    void titleForPrefersRealMetadata();
    void titleForParsesMpvFilenameFallback();
};

// U+00B7 MIDDLE DOT / U+2013 EN DASH written as \u escapes throughout this
// file (not embedded as literal UTF-8 source bytes) for the same reason as
// MediaNameFormatter.cpp: no BOM, and MSVC's source-charset guess is not
// something to depend on.
void TestMediaNameFormatter::episodeWithTitle()
{
    QCOMPARE(MediaNameFormatter::cleaned(
                 QStringLiteral("Show.Name.S01E02.The.Reckoning.1080p.WEB-DL.mkv")),
             QStringLiteral("Show Name  \u00b7  S01E02  \u00b7  The Reckoning"));
}

void TestMediaNameFormatter::episodeNoTitle()
{
    QCOMPARE(MediaNameFormatter::cleaned(
                 QStringLiteral("Show.Name.S01E02.1080p.WEB-DL.DDP5.1.x264-GRP.mkv")),
             QStringLiteral("Show Name  \u00b7  S01E02"));
}

void TestMediaNameFormatter::movieWithYear()
{
    QCOMPARE(MediaNameFormatter::cleaned(
                 QStringLiteral("Movie.Name.2019.1080p.BluRay.x264-GRP.mkv")),
             QStringLiteral("Movie Name (2019)"));
}

void TestMediaNameFormatter::animeFansubOrder()
{
    const auto p = MediaNameFormatter::parse(
        QStringLiteral("[Group] Show Name - 12 [1080p][A1B2C3D4].mkv"));
    QCOMPARE(p.show, QStringLiteral("Show Name"));
    QCOMPARE(p.episode, QStringLiteral("12"));
}

void TestMediaNameFormatter::numericShowName()
{
    const auto p24 = MediaNameFormatter::parse(QStringLiteral("24.S01E01.1080p.mkv"));
    QCOMPARE(p24.show, QStringLiteral("24"));
    QCOMPARE(p24.season, QStringLiteral("01"));

    const auto p911 = MediaNameFormatter::parse(QStringLiteral("9-1-1.S02E03.1080p.mkv"));
    QVERIFY(p911.show.contains(QStringLiteral("9")));
    QCOMPARE(p911.season, QStringLiteral("02"));
}

void TestMediaNameFormatter::yearInShowName()
{
    // A series, not a 2019 movie: the episode rule must win.
    const auto p = MediaNameFormatter::parse(QStringLiteral("Show.Name.2019.S01E02.mkv"));
    QCOMPARE(p.show, QStringLiteral("Show Name 2019"));
    QCOMPARE(p.season, QStringLiteral("01"));
    QVERIFY(p.year.isEmpty());
}

void TestMediaNameFormatter::dailyShowDate()
{
    // "2024.03.15" is a date, not a release year -- must not trip the
    // movie-year rule, and (with no episode marker either) falls back to
    // the tag-stripped filename rather than inventing a year.
    const auto p = MediaNameFormatter::parse(QStringLiteral("Show.Name.2024.03.15.mkv"));
    QVERIFY(p.year.isEmpty());
    QVERIFY(p.season.isEmpty());
}

void TestMediaNameFormatter::resolutionVsEpisodeCode()
{
    const auto p = MediaNameFormatter::parse(QStringLiteral("Show.Name.1x02.4K.mkv"));
    QCOMPARE(p.season, QStringLiteral("01"));
    QCOMPARE(p.episode, QStringLiteral("02"));
}

void TestMediaNameFormatter::nonLatinPassThrough()
{
    const QString name = QStringLiteral("\u30a2\u30cb\u30e1.S01E01.1080p.mkv");
    const auto p = MediaNameFormatter::parse(name);
    QCOMPARE(p.show, QStringLiteral("\u30a2\u30cb\u30e1"));
    QCOMPARE(p.episode, QStringLiteral("01"));
}

void TestMediaNameFormatter::alreadyCleanIsIdempotent()
{
    const QString clean = QStringLiteral("Interview with a friend.mkv");
    const QString once = MediaNameFormatter::cleaned(clean);
    QCOMPARE(once, QStringLiteral("Interview with a friend"));
    // Feeding the cleaned output back through must not mangle it further.
    QCOMPARE(MediaNameFormatter::cleaned(once), once);
}

void TestMediaNameFormatter::multiEpisode()
{
    const auto p = MediaNameFormatter::parse(QStringLiteral("Show.Name.S01E02E03.1080p.mkv"));
    QCOMPARE(p.season, QStringLiteral("01"));
    QCOMPARE(p.episode, QStringLiteral("02"));
    QCOMPARE(p.episodeEnd, QStringLiteral("03"));
    QCOMPARE(MediaNameFormatter::cleaned(QStringLiteral("Show.Name.S01E02E03.1080p.mkv")),
             QStringLiteral("Show Name  \u00b7  S01E02\u2013E03"));
}

void TestMediaNameFormatter::emptyAndBareExtension()
{
    QCOMPARE(MediaNameFormatter::cleaned(QString()), QString());
    QCOMPARE(MediaNameFormatter::baseName(QStringLiteral("C:/movies/Show.mkv")),
             QStringLiteral("Show.mkv"));
    QCOMPARE(MediaNameFormatter::baseName(QStringLiteral("/movies/Show.mkv")),
             QStringLiteral("Show.mkv"));
}

void TestMediaNameFormatter::spacedNameDotsUntouched()
{
    // A filename with real spaces already must not have interior dots (an
    // abbreviation, say) mangled by the dot-to-space rule.
    QCOMPARE(MediaNameFormatter::cleaned(QStringLiteral("Mr. Robot S01E01 1080p.mkv")),
             QStringLiteral("Mr. Robot  \u00b7  S01E01"));
}

void TestMediaNameFormatter::displayTogglesCleanup()
{
    const QString path = QStringLiteral("Show.Name.S01E02.1080p.mkv");
    QCOMPARE(MediaNameFormatter::display(path, true), MediaNameFormatter::cleaned(path));
    QCOMPARE(MediaNameFormatter::display(path, false), MediaNameFormatter::baseName(path));
}

void TestMediaNameFormatter::titleForPrefersRealMetadata()
{
    // A title that is neither empty nor equal to the filename is real
    // container metadata -- trust it verbatim, do not run it through the
    // parser (it is not a release-scene filename).
    MediaNameFormatter fmt;
    QCOMPARE(fmt.titleFor(QStringLiteral("The Reckoning"),
                          QStringLiteral("Show.Name.S01E02.1080p.mkv")),
             QStringLiteral("The Reckoning"));
}

void TestMediaNameFormatter::titleForParsesMpvFilenameFallback()
{
    // mpv's own "media-title" fallback is the filename WITH extension (not
    // stripped) -- this is the exact case that shipped broken once: the
    // comparison must match baseName(), not a stripped name, or every
    // fallback title is (wrongly) treated as real metadata and the parser
    // never runs.
    MediaNameFormatter fmt;
    const QString path = QStringLiteral("Show.Name.S01E02.The.Reckoning.1080p.WEB-DL.mkv");
    QCOMPARE(fmt.titleFor(MediaNameFormatter::baseName(path), path),
             MediaNameFormatter::cleaned(path));

    fmt.setCleanupEnabled(false);
    QCOMPARE(fmt.titleFor(MediaNameFormatter::baseName(path), path),
             MediaNameFormatter::baseName(path));
}

QTEST_MAIN(TestMediaNameFormatter)
#include "test_medianame.moc"
