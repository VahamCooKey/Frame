#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>

// Development-only exporter. No dependency is added to the player.
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    QTextStream error(stderr);
    if (args.size() < 4) {
        error << "Usage: frame-icon-export source.svg output-prefix size [size...]\n";
        return 1;
    }
    QSvgRenderer svg(args[1]);
    if (!svg.isValid()) {
        error << "Invalid SVG: " << args[1] << '\n';
        return 2;
    }
    for (int i = 3; i < args.size(); ++i) {
        bool ok = false;
        const int size = args[i].toInt(&ok);
        if (!ok || size < 16 || size > 256) return 3;
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            svg.render(&painter, QRectF(0, 0, size, size));
        }
        const QString target = args[2] + '-' + QString::number(size) + ".png";
        if (!image.save(target)) {
            error << "Cannot write: " << target << '\n';
            return 4;
        }
    }
    return 0;
}
