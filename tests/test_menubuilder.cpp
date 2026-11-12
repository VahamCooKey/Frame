#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "input/MenuBuilder.h"

namespace {

bool writeFile(const QString &path, const QString &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream stream(&file);
    stream << content;
    return true;
}

QVariantMap findNode(const QVariantList &tree, const QStringList &path)
{
    QVariantList level = tree;
    for (int i = 0; i < path.size(); ++i) {
        QVariantMap found;
        for (const QVariant &item : level) {
            const QVariantMap map = item.toMap();
            if (map.value(QStringLiteral("text")).toString() == path.at(i)) {
                found = map;
                break;
            }
        }
        if (found.isEmpty())
            return {};
        if (i == path.size() - 1)
            return found;
        level = found.value(QStringLiteral("children")).toList();
    }
    return {};
}

} // namespace

class TestMenuBuilder : public QObject {
    Q_OBJECT

private slots:
    void buildAndOverride();
    void malformedLinesDoNotCrash();
    void singleFileApi();
    void emptyAndCommentOnlyFiles();
    void bindingTableCoversEveryBinding();
    void watchFilesEmitsOnValidChange();
    void watchFilesSilentOnBrokenChange();
};

void TestMenuBuilder::buildAndOverride()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString conf1 = dir.filePath(QStringLiteral("input1.conf"));
    const QString conf2 = dir.filePath(QStringLiteral("input2.conf"));
    QVERIFY(writeFile(conf1, QStringLiteral(
        "SPACE cycle pause                              #menu: Playback > Play/Pause\n"
        "Ctrl+r cycle-values video-rotate 90 180 270 0  #menu: Video > Rotate\n"
        "o script-message app-open-file                 #menu: File > Open...\n"
        ", frame-back-step\n"
        "this line has no menu marker\n")));
    QVERIFY(writeFile(conf2, QStringLiteral(
        "Ctrl+p script-message app-pause              #menu: Playback > Play/Pause\n"
        "] multiply speed 1.1                          #menu: Playback > Faster\n")));

    MenuBuilder builder;
    const QVariantList root = builder.buildMenu({conf1, conf2});

    // Root order: first appearance across files.
    QCOMPARE(root.size(), 3);
    QCOMPARE(root.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Playback"));
    QCOMPARE(root.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Video"));
    QCOMPARE(root.at(2).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("File"));

    // Branches have children, no command; children keep insertion order.
    const QVariantMap playback = root.at(0).toMap();
    QVERIFY(playback.contains(QStringLiteral("children")));
    QVERIFY(!playback.contains(QStringLiteral("command")));
    const QVariantList playbackChildren = playback.value(QStringLiteral("children")).toList();
    QCOMPARE(playbackChildren.size(), 2);
    QCOMPARE(playbackChildren.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Play/Pause"));
    QCOMPARE(playbackChildren.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Faster"));

    // Override from the second file applied to the existing path, key and
    // command taken from the winning line (A10).
    QCOMPARE(playbackChildren.at(0).toMap().value(QStringLiteral("command")).toString(),
             QStringLiteral("script-message app-pause"));
    QCOMPARE(playbackChildren.at(1).toMap().value(QStringLiteral("command")).toString(),
             QStringLiteral("multiply speed 1.1"));

    // Nesting depth: "Video > Rotate" is a leaf two levels deep.
    const QVariantMap rotate = findNode(root, {QStringLiteral("Video"), QStringLiteral("Rotate")});
    QVERIFY(!rotate.isEmpty());
    QVERIFY(rotate.contains(QStringLiteral("command")));
    QVERIFY(!rotate.contains(QStringLiteral("children")));
    QCOMPARE(rotate.value(QStringLiteral("command")).toString(),
             QStringLiteral("cycle-values video-rotate 90 180 270 0"));

    const QVariantMap open = findNode(root, {QStringLiteral("File"), QStringLiteral("Open...")});
    QCOMPARE(open.value(QStringLiteral("command")).toString(),
             QStringLiteral("script-message app-open-file"));

    // Leaf keys (A10): the mpv key token of the same binding line as the
    // #menu: comment — plain key, symbol key, modifier key. The override
    // replaced Play/Pause's key with the second file's Ctrl+p. Branch items
    // stay keyless.
    QCOMPARE(open.value(QStringLiteral("key")).toString(), QStringLiteral("o"));
    QCOMPARE(rotate.value(QStringLiteral("key")).toString(), QStringLiteral("Ctrl+r"));
    QCOMPARE(playbackChildren.at(1).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("]"));
    QCOMPARE(playbackChildren.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("Ctrl+p"));
    QVERIFY(!playback.contains(QStringLiteral("key")));
}

void TestMenuBuilder::malformedLinesDoNotCrash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString conf = dir.filePath(QStringLiteral("broken.conf"));
    QVERIFY(writeFile(conf, QStringLiteral(
        "orphan #menu:\n"          // empty menu path
        "lonely #menu: Weird > Solo\n" // key only, no command
        "   \n"
        "# just a comment\n"
        "key command #menu: A > > B\n" // empty segment collapses
    )));

    MenuBuilder builder;
    const QVariantList root = builder.buildMenu({conf});

    QCOMPARE(root.size(), 1);
    QCOMPARE(root.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("A"));
    QVERIFY(findNode(root, {QStringLiteral("Weird")}).isEmpty());

    // Nonexistent file and empty file list are safe no-ops.
    QVERIFY(builder.buildMenu({dir.filePath(QStringLiteral("missing.conf"))}).isEmpty());
    QVERIFY(builder.buildMenu({}).isEmpty());
}

void TestMenuBuilder::singleFileApi()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString conf = dir.filePath(QStringLiteral("one.conf"));
    QVERIFY(writeFile(conf, QStringLiteral(
        "SPACE cycle pause              #menu: Playback > Play/Pause\n"
        "f script-message app-toggle-fullscreen  #menu: Video > Fullscreen\n"
    )));

    MenuBuilder builder;
    const QVariantList root = builder.buildMenuFromConf(conf);

    QCOMPARE(root.size(), 2);
    QCOMPARE(root.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Playback"));
    QCOMPARE(findNode(root, {QStringLiteral("Playback"), QStringLiteral("Play/Pause")})
                 .value(QStringLiteral("command")).toString(),
             QStringLiteral("cycle pause"));
}

void TestMenuBuilder::emptyAndCommentOnlyFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString emptyConf = dir.filePath(QStringLiteral("empty.conf"));
    const QString commentsConf = dir.filePath(QStringLiteral("comments.conf"));
    QVERIFY(writeFile(emptyConf, QString()));
    QVERIFY(writeFile(commentsConf, QStringLiteral(
        "# Frame - mpv input.conf\n"
        "# nothing but comments and blanks\n"
        "\n"
        "   \n"
        "#menu: orphaned marker comment is still just a comment\n"
        "# SPACE cycle pause  #menu: Ghost > Entry\n")));

    MenuBuilder builder;
    QVERIFY(builder.buildMenu({emptyConf}).isEmpty());
    QVERIFY(builder.buildMenu({commentsConf}).isEmpty());
    QVERIFY(builder.buildMenu({emptyConf, commentsConf}).isEmpty());
}

void TestMenuBuilder::bindingTableCoversEveryBinding()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString conf1 = dir.filePath(QStringLiteral("table1.conf"));
    const QString conf2 = dir.filePath(QStringLiteral("table2.conf"));
    QVERIFY(writeFile(conf1, QStringLiteral(
        "SPACE cycle pause                       #menu: Playback > Play/Pause\n"
        ", frame-back-step\n"
        "# x quit\n"
        "   \n"
        "lonely\n"
        "f script-message app-toggle-fullscreen  #menu: Video > Fullscreen\n")));
    QVERIFY(writeFile(conf2, QStringLiteral(
        "SPACE script-message app-pause          #menu: Playback > Play/Pause\n"
        "] multiply speed 1.1                    #menu: Playback > Faster\n")));

    MenuBuilder builder;
    const QVariantList rows = builder.bindingTable({conf1, conf2});
    QCOMPARE(rows.size(), 4);

    // A binding with no "#menu:" comment is still rebindable, so it is in the
    // table with an empty menuPath. A commented-out one is not a binding at
    // all, and a key with no command is not one either.
    QCOMPARE(rows.at(1).toMap().value(QStringLiteral("key")).toString(), QStringLiteral(","));
    QCOMPARE(rows.at(1).toMap().value(QStringLiteral("command")).toString(),
             QStringLiteral("frame-back-step"));
    QVERIFY(rows.at(1).toMap().value(QStringLiteral("menuPath")).toString().isEmpty());
    for (const QVariant &row : rows) {
        const QString key = row.toMap().value(QStringLiteral("key")).toString();
        QVERIFY(key != QStringLiteral("x"));
        QVERIFY(key != QStringLiteral("lonely"));
    }

    // The second file's SPACE replaces the first file's row in place: later
    // command, original position.
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("SPACE"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("command")).toString(),
             QStringLiteral("script-message app-pause"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("menuPath")).toString(),
             QStringLiteral("Playback > Play/Pause"));

    // Remaining rows keep file order, the new key appended last.
    QCOMPARE(rows.at(2).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("f"));
    QCOMPARE(rows.at(2).toMap().value(QStringLiteral("menuPath")).toString(),
             QStringLiteral("Video > Fullscreen"));
    QCOMPARE(rows.at(3).toMap().value(QStringLiteral("key")).toString(), QStringLiteral("]"));
    QCOMPARE(rows.at(3).toMap().value(QStringLiteral("command")).toString(),
             QStringLiteral("multiply speed 1.1"));

    QVERIFY(builder.bindingTable({dir.filePath(QStringLiteral("missing.conf"))}).isEmpty());
    QVERIFY(builder.bindingTable({}).isEmpty());
}

void TestMenuBuilder::watchFilesEmitsOnValidChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString conf = dir.filePath(QStringLiteral("watched.conf"));
    QVERIFY(writeFile(conf, QStringLiteral(
        "SPACE cycle pause  #menu: Playback > Play/Pause\n")));

    MenuBuilder builder;
    builder.watchFiles({conf});
    // Let QFileSystemWatcher finish registering the path before changing it;
    // Windows can otherwise coalesce the immediate initial write.
    QTest::qWait(100);

    QSignalSpy spy(&builder, &MenuBuilder::menuChanged);
    QVERIFY(spy.isValid());

    QVERIFY(writeFile(conf, QStringLiteral(
        "SPACE cycle pause  #menu: Playback > Play/Pause\n"
        "f script-message app-toggle-fullscreen  #menu: Video > Fullscreen\n")));

    // Debounce is 250 ms; allow generous slack for the watcher round-trip.
    QVERIFY(spy.wait(5000));
}

void TestMenuBuilder::watchFilesSilentOnBrokenChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString conf = dir.filePath(QStringLiteral("watched.conf"));
    QVERIFY(writeFile(conf, QStringLiteral(
        "SPACE cycle pause  #menu: Playback > Play/Pause\n")));

    MenuBuilder builder;
    builder.watchFiles({conf});
    QTest::qWait(100);

    QSignalSpy spy(&builder, &MenuBuilder::menuChanged);
    QVERIFY(spy.isValid());

    // A change that parses to an empty menu must not emit (staged parse).
    QVERIFY(writeFile(conf, QStringLiteral("# all bindings gone\n")));

    // Wait comfortably past the 250 ms debounce; nothing may arrive.
    QVERIFY(!spy.wait(1500));
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestMenuBuilder)
#include "test_menubuilder.moc"
