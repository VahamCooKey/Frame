#include <QtTest>
#include "core/TrackPreference.h"

namespace {
QVariantMap track(int id, const QString &language, const QString &title = {},
                  const QString &type = QStringLiteral("audio"))
{
    return {{"id", id}, {"lang", language}, {"title", title}, {"type", type}};
}
}

class TestTrackPreference : public QObject {
    Q_OBJECT
private slots:
    void reorderedTracks();
    void languageAliasesAndRegions();
    void distinguishRoles_data();
    void distinguishRoles();
    void missingAndInvalidChoices();
    void untaggedLayouts();
    void ambiguousMatches();
    void offIsExplicit();
};

void TestTrackPreference::reorderedTracks()
{
    QVariantList first, second;
    const QStringList languages{"en", "es", "fr", "de", "hi", "ta", "ja", "bn", "ko", "zh"};
    for (int i = 0; i < languages.size(); ++i) {
        first.append(track(i + 1, languages[i]));
        second.prepend(track(30 - i, languages[i]));
    }
    TrackPreference preference;
    QVERIFY(preference.remember(first, "audio", 7));
    QCOMPARE(preference.matchingId(second, "audio"), 24);
}

void TestTrackPreference::languageAliasesAndRegions()
{
    TrackPreference preference;
    QVERIFY(preference.remember({track(1, "fre")}, "audio", 1));
    QCOMPARE(preference.matchingId({track(5, "fra"), track(9, "eng")}, "audio"), 5);
    QVERIFY(preference.remember({track(1, "pt_BR")}, "audio", 1));
    QCOMPARE(preference.matchingId({track(2, "pt-PT"), track(4, "por-br")}, "audio"), 4);
    QVERIFY(preference.remember({track(1, "pt-BR", "Original")}, "audio", 1));
    QCOMPARE(preference.matchingId({track(2, "pt-PT", "Original"), track(4, "por-br", "Dub")}, "audio"), 4);
    QVERIFY(preference.remember({track(1, "eng", "  Main   Mix  ")}, "audio", 1));
    QCOMPARE(preference.matchingId({track(8, "en", "main mix"), track(1, "ja")}, "audio"), 8);
}

void TestTrackPreference::distinguishRoles_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("flag");
    QTest::newRow("commentary") << "audio" << "Director commentary" << "";
    QTest::newRow("description") << "audio" << "Audio description" << "visual-impaired";
    QTest::newRow("forced") << "sub" << "English forced" << "forced";
    QTest::newRow("captions") << "sub" << "English SDH" << "hearing-impaired";
    QTest::newRow("signs") << "sub" << "Signs & songs" << "";
}

void TestTrackPreference::distinguishRoles()
{
    QFETCH(QString, type);
    QFETCH(QString, title);
    QFETCH(QString, flag);
    TrackPreference preference;
    QVERIFY(preference.remember({track(1, "eng", title, type)}, type, 1));
    auto match = track(4, "en", flag.isEmpty() ? title : QString(), type);
    if (!flag.isEmpty()) match[flag] = true;
    QCOMPARE(preference.matchingId({track(1, "en", {}, type), match}, type), 4);
    QCOMPARE(preference.matchingId({track(1, "en", {}, type)}, type), -1);
}

void TestTrackPreference::missingAndInvalidChoices()
{
    TrackPreference preference;
    QVERIFY(preference.remember({track(7, "jpn")}, "audio", 7));
    QCOMPARE(preference.matchingId({track(7, "eng")}, "audio"), -1);
    QVERIFY(!preference.remember({track(1, "eng")}, "audio", 99));
    QCOMPARE(preference.matchingId({track(3, "ja")}, "audio"), 3);
}

void TestTrackPreference::untaggedLayouts()
{
    TrackPreference preference;
    QVERIFY(preference.remember({track(1, {}), track(2, {})}, "audio", 2));
    QCOMPARE(preference.matchingId({track(4, {}), track(8, {})}, "audio"), 8);
    QCOMPARE(preference.matchingId({track(2, {})}, "audio"), -1);
    QCOMPARE(preference.matchingId({track(1, "en"), track(2, "fr")}, "audio"), -1);
}

void TestTrackPreference::ambiguousMatches()
{
    TrackPreference preference;
    QVERIFY(preference.remember({track(1, "eng", "Original mix")}, "audio", 1));
    QCOMPARE(preference.matchingId({track(2, "en", "Mix A"), track(3, "en", "Mix B")}, "audio"), -1);
}

void TestTrackPreference::offIsExplicit()
{
    TrackPreference preference;
    QCOMPARE(preference.mode(), TrackPreference::Mode::Unset);
    QVERIFY(preference.remember({}, "sub", -1));
    QCOMPARE(preference.mode(), TrackPreference::Mode::Off);
    QCOMPARE(preference.matchingId({track(2, "en", {}, "sub")}, "sub"), -1);
    QVERIFY(preference.remember({track(2, "en", {}, "sub")}, "sub", 2));
    QCOMPARE(preference.mode(), TrackPreference::Mode::Selected);
}

QTEST_GUILESS_MAIN(TestTrackPreference)
#include "test_trackpreference.moc"
