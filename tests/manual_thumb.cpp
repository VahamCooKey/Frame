// Standalone diagnostic for ThumbnailGenerator (v0.2 thumbnail preview).
// Compile manually; results appended to build-tmp\thumb-probe.log.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QColor>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <array>

#include "core/ThumbnailGenerator.h"

namespace {
QString findAsset(const QString &relative)
{
    const QStringList candidates{relative,
                                 QStringLiteral("../") + relative,
                                 QStringLiteral("../../") + relative};
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c))
            return QFileInfo(c).absoluteFilePath();
    }
    return {};
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QFile log(QStringLiteral("build-tmp/thumb-probe.log"));
    log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    QTextStream out(&log);
    auto mark = [&out](const QString &s) { out << "[probe] " << s << '\n'; out.flush(); };

    const QString video = findAsset(QStringLiteral("assets/test/sample.mkv"));
    if (video.isEmpty()) {
        mark(QStringLiteral("asset not found"));
        return 2;
    }
    mark(QStringLiteral("video=%1").arg(video));

    ThumbnailGenerator thumbs;
    const std::array<double, 3> samples{1.0, 3.0, 6.0};
    std::array<QByteArray, 3> hashes;
    int sampleIndex = 0;
    QObject::connect(&thumbs, &ThumbnailGenerator::thumbnailReady,
                     &app, [&](const QString &file, double seconds, const QImage &image, bool success) {
                         mark(QStringLiteral("RESULT file=%1 sec=%2 success=%3 null=%4 w=%5 h=%6")
                                  .arg(file)
                                  .arg(seconds)
                                  .arg(success ? 1 : 0)
                                  .arg(image.isNull() ? 1 : 0)
                                  .arg(image.width())
                                  .arg(image.height()));
                         if (!success) {
                             app.exit(3);
                             return;
                         }

                         if (sampleIndex == 0) {
                             image.save(QStringLiteral("build-tmp/thumb-current.png"));
                             const QColor sample = image.pixelColor(image.width() / 2, image.height() / 2);
                             mark(QStringLiteral("sample rgb=%1,%2,%3")
                                      .arg(sample.red()).arg(sample.green()).arg(sample.blue()));
                         }

                         hashes[static_cast<size_t>(sampleIndex)] = QCryptographicHash::hash(
                             QByteArray(reinterpret_cast<const char *>(image.constBits()),
                                        static_cast<qsizetype>(image.sizeInBytes())),
                             QCryptographicHash::Sha256);
                         ++sampleIndex;
                         if (sampleIndex == static_cast<int>(samples.size())) {
                             const bool distinct = hashes[0] != hashes[1]
                                                   && hashes[1] != hashes[2]
                                                   && hashes[0] != hashes[2];
                             mark(QStringLiteral("frames distinct=%1").arg(distinct ? "yes" : "no"));
                             app.exit(distinct ? 0 : 5);
                             return;
                         }

                         QTimer::singleShot(0, &thumbs, [&]() {
                             thumbs.requestThumbnail(video, samples[static_cast<size_t>(sampleIndex)], 240);
                         });
                     });

    QTimer::singleShot(300, &app, [&]() {
        mark(QStringLiteral("requesting thumbnails at 1.0s, 3.0s, 6.0s (maxw=240)"));
        thumbs.requestThumbnail(video, samples[0], 240);
    });
    QTimer::singleShot(15000, &app, [&]() {
        mark(QStringLiteral("TIMEOUT - no thumbnailReady in 15s"));
        app.exit(4);
    });

    return app.exec();
}
