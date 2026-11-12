#include <QtTest>
#include <QSignalSpy>

#include "core/AudioOverview.h"

#include <array>

// Waveform peaks and the tag/cover/waveform pipeline on generated audio.
class TestAudioOverview : public QObject {
    Q_OBJECT

private slots:
    void peaksAreNormalisedPerBucket()
    {
        // Four buckets of two samples; the loudest bucket becomes 1.0.
        const std::array<qint16, 8> samples{100, -200, 0, 50, -1000, 400, 0, 0};
        const QVariantList peaks = AudioOverview::peaksFromSamples(samples.data(), samples.size(), 4);
        QCOMPARE(peaks.size(), 4);
        QCOMPARE(peaks.at(2).toDouble(), 1.0);
        QCOMPARE(peaks.at(3).toDouble(), 0.0);
        QVERIFY(peaks.at(0).toDouble() > peaks.at(1).toDouble());
        for (const QVariant &v : peaks)
            QVERIFY(v.toDouble() >= 0.0 && v.toDouble() <= 1.0);
    }

    void peaksHandleEdgeCases()
    {
        QVERIFY(AudioOverview::peaksFromSamples(nullptr, 0, 10).isEmpty());
        const std::array<qint16, 3> silent{0, 0, 0};
        const QVariantList flat = AudioOverview::peaksFromSamples(silent.data(), silent.size(), 10);
        // Never more buckets than samples; silence stays at zero.
        QCOMPARE(flat.size(), 3);
        for (const QVariant &v : flat)
            QCOMPARE(v.toDouble(), 0.0);
        // The most negative sample must not overflow.
        const std::array<qint16, 2> extreme{-32768, 16384};
        const QVariantList e = AudioOverview::peaksFromSamples(extreme.data(), extreme.size(), 2);
        QCOMPARE(e.at(0).toDouble(), 1.0);
    }

    void nonLocalPathsClear()
    {
        AudioOverview overview;
        overview.load(QStringLiteral("https://example.invalid/a.mp3"));
        QVERIFY(overview.path().isEmpty());
        overview.load(QStringLiteral("Z:/definitely/missing/file.mp3"));
        QVERIFY(overview.path().isEmpty());
    }

    void fixtureProducesTagsAndPeaks()
    {
        const QString file = QFINDTESTDATA("fixtures/audio-tagged.mp3");
        if (file.isEmpty())
            QSKIP("fixtures/audio-tagged.mp3 not present");
        AudioOverview overview;
        QSignalSpy peaks(&overview, &AudioOverview::peaksChanged);
        overview.load(file);
        QTRY_VERIFY_WITH_TIMEOUT(!overview.peaks().isEmpty(), 20000);
        QCOMPARE(overview.title(), QStringLiteral("Frame Fixture"));
        QCOMPARE(overview.artist(), QStringLiteral("Frame Tests"));
        QCOMPARE(overview.peaks().size(), AudioOverview::PeakCount);
        QVERIFY(!overview.coverSource().isEmpty());
    }
};

QTEST_MAIN(TestAudioOverview)
#include "test_audiooverview.moc"
