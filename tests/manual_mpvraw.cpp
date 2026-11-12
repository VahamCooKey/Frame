// Direct mpv screenshot-raw probe (no ThumbnailGenerator): proves whether
// screenshot-raw works headless with vo=null and what node format it returns.
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <mpv/client.h>

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

    QFile log(QStringLiteral("build-tmp/mpvraw-probe.log"));
    log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    QTextStream out(&log);
    auto mark = [&out](const QString &s) { out << "[raw] " << s << '\n'; out.flush(); };

    const QString video = findAsset(QStringLiteral("assets/test/sample.mkv"));
    if (video.isEmpty()) {
        mark(QStringLiteral("asset missing"));
        return 2;
    }

    mpv_handle *mpv = mpv_create();
    mpv_set_option_string(mpv, "vo", "null");
    mpv_set_option_string(mpv, "hwdec", "no");
    mpv_set_option_string(mpv, "audio", "no");
    mpv_set_option_string(mpv, "idle", "yes");
    mpv_set_option_string(mpv, "load-scripts", "no");
    mpv_set_option_string(mpv, "terminal", "no");
    mpv_set_option_string(mpv, "config", "no");
    mpv_set_option_string(mpv, "osd-level", "0");
    mpv_set_option_string(mpv, "osc", "no");
    if (mpv_initialize(mpv) < 0) {
        mark(QStringLiteral("mpv_initialize failed"));
        return 1;
    }
    mark(QStringLiteral("initialized"));

    const char *load[] = {"loadfile", video.toUtf8().constData(), nullptr};
    mark(QStringLiteral("loadfile rc=%1").arg(mpv_command(mpv, load)));

    // Wait for FILE_LOADED.
    bool loaded = false;
    for (int i = 0; i < 60; ++i) {
        mpv_event *e = mpv_wait_event(mpv, 0.05);
        if (e->event_id == MPV_EVENT_FILE_LOADED) {
            loaded = true;
            break;
        }
    }
    mark(QStringLiteral("FILE_LOADED=%1").arg(loaded ? "yes" : "no"));

    const char *seek[] = {"seek", "3.0", "absolute", nullptr};
    mark(QStringLiteral("seek rc=%1").arg(mpv_command(mpv, seek)));
    for (int i = 0; i < 30; ++i)
        mpv_wait_event(mpv, 0.05);

    // screenshot-raw with explicit mpv_node args.
    mpv_node argStrings[1];
    argStrings[0].format = MPV_FORMAT_STRING;
    argStrings[0].u.string = const_cast<char *>("screenshot-raw");
    mpv_node_list argsList{};
    argsList.num = 1;
    argsList.values = argStrings;
    mpv_node argsNode{};
    argsNode.format = MPV_FORMAT_NODE_ARRAY;
    argsNode.u.list = &argsList;

    mpv_node result{};
    int rc = mpv_command_node(mpv, &argsNode, &result);
    mark(QStringLiteral("screenshot-raw rc=%1 resultFormat=%2")
             .arg(rc)
             .arg(result.format));
    if (result.format == MPV_FORMAT_NODE_MAP && result.u.list) {
        mark(QStringLiteral("NODE_MAP num=%1").arg(result.u.list->num));
        for (int i = 0; i < result.u.list->num; ++i) {
            const char *key = result.u.list->keys[i];
            const mpv_node &v = result.u.list->values[i];
            if (v.format == MPV_FORMAT_BYTE_ARRAY && v.u.ba) {
                mark(QStringLiteral("  key=%1 BYTE_ARRAY size=%2")
                         .arg(QString::fromUtf8(key))
                         .arg(v.u.ba->size));
                const QImage img = QImage::fromData(
                    QByteArray(static_cast<const char *>(v.u.ba->data),
                               static_cast<int>(v.u.ba->size)),
                    "BMP");
                mark(QStringLiteral("  QImage null=%1 w=%2 h=%3")
                         .arg(img.isNull() ? 1 : 0)
                         .arg(img.width())
                         .arg(img.height()));
            } else {
                mark(QStringLiteral("  key=%1 fmt=%2")
                         .arg(QString::fromUtf8(key))
                         .arg(int(v.format)));
            }
        }
    }
    mpv_free_node_contents(&result);
    mpv_terminate_destroy(mpv);
    mark(QStringLiteral("done"));
    return 0;
}
