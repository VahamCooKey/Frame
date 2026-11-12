#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QWindow>
#include <QKeyEvent>

#include "input/ShortcutManager.h"

class TestKeymap : public QObject {
    Q_OBJECT

private slots:
    void keyNames_data();
    void keyNames();
    void confFileSanity();
    void shortcutHintsFollowBindings();
    void focusedTextOwnsKeys();
};

void TestKeymap::keyNames_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("mods");
    QTest::addColumn<QString>("expected");

    QTest::newRow("space")
        << int(Qt::Key_Space) << int(Qt::NoModifier) << QStringLiteral("SPACE");
    QTest::newRow("ctrl_r")
        << int(Qt::Key_R) << int(Qt::ControlModifier) << QStringLiteral("Ctrl+r");
    QTest::newRow("plain_a")
        << int(Qt::Key_A) << int(Qt::NoModifier) << QStringLiteral("a");
    QTest::newRow("plain_f")
        << int(Qt::Key_F) << int(Qt::NoModifier) << QStringLiteral("f");
    QTest::newRow("f5")
        << int(Qt::Key_F5) << int(Qt::NoModifier) << QStringLiteral("F5");
    QTest::newRow("right")
        << int(Qt::Key_Right) << int(Qt::NoModifier) << QStringLiteral("RIGHT");
    QTest::newRow("pgup")
        << int(Qt::Key_PageUp) << int(Qt::NoModifier) << QStringLiteral("PGUP");
    QTest::newRow("shift_pgdwn")
        << int(Qt::Key_PageDown) << int(Qt::ShiftModifier) << QStringLiteral("Shift+PGDWN");
    QTest::newRow("media_play")
        << int(Qt::Key_MediaPlay) << int(Qt::NoModifier) << QStringLiteral("PLAY");
    QTest::newRow("key_none")
        << int(Qt::Key_unknown) << int(Qt::NoModifier) << QString();
    QTest::newRow("digit_1")
        << int(Qt::Key_1) << int(Qt::NoModifier) << QStringLiteral("1");
    QTest::newRow("ctrl_shift_e")
        << int(Qt::Key_E) << int(Qt::ControlModifier | Qt::ShiftModifier)
        << QStringLiteral("Ctrl+Shift+e");
}

void TestKeymap::keyNames()
{
    QFETCH(int, key);
    QFETCH(int, mods);
    QFETCH(QString, expected);

    QCOMPARE(ShortcutManager::toMpvKeyName(key, Qt::KeyboardModifiers(QFlag(mods))), expected);
}

void TestKeymap::confFileSanity()
{
    const QStringList candidates{
        QStringLiteral("assets/input.conf"),
        QStringLiteral("../assets/input.conf"),
        QStringLiteral("../../assets/input.conf"),
    };

    QString confPath;
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            confPath = candidate;
            break;
        }
    }
    if (confPath.isEmpty())
        QSKIP("assets/input.conf not found relative to the test working directory");

    QFile file(confPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));

    int bindingLines = 0;
    int menuLines = 0;
    QHash<QString, QString> commandFor;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(u'#'))
            continue;
        ++bindingLines;
        const QString keyAndCommand = trimmed.section(u'#', 0, 0).simplified();
        commandFor.insert(keyAndCommand.section(u' ', 0, 0), keyAndCommand.section(u' ', 1));

        const qsizetype marker = line.indexOf(QStringLiteral("#menu:"));
        if (marker < 0)
            continue;
        ++menuLines;

        // Every "#menu:" line must have a command part after the key token.
        const QString binding = line.left(marker);
        qsizetype i = 0;
        while (i < binding.size() && binding.at(i).isSpace())
            ++i;
        while (i < binding.size() && !binding.at(i).isSpace())
            ++i;
        while (i < binding.size() && binding.at(i).isSpace())
            ++i;
        QVERIFY2(i < binding.size(),
                 qPrintable(QStringLiteral("menu line without command: %1").arg(line)));
    }

    QVERIFY(bindingLines >= 20);
    // Next/previous video are single keys; chapters take the Shift variants.
    QCOMPARE(commandFor.value(QStringLiteral("PGUP")), QStringLiteral("playlist-prev"));
    QCOMPARE(commandFor.value(QStringLiteral("PGDWN")), QStringLiteral("playlist-next"));
    QCOMPARE(commandFor.value(QStringLiteral("Shift+PGUP")), QStringLiteral("add chapter -1"));
    QCOMPARE(commandFor.value(QStringLiteral("Shift+PGDWN")), QStringLiteral("add chapter 1"));
    // JKL: J slower, K play/pause, L faster; Backspace resets the speed.
    QCOMPARE(commandFor.value(QStringLiteral("j")), QStringLiteral("script-message app-shuttle-down"));
    QCOMPARE(commandFor.value(QStringLiteral("k")), QStringLiteral("cycle pause"));
    QCOMPARE(commandFor.value(QStringLiteral("l")), QStringLiteral("script-message app-shuttle-up"));
    QCOMPARE(commandFor.value(QStringLiteral("BS")), QStringLiteral("set speed 1.0"));
    QCOMPARE(commandFor.value(QStringLiteral("F1")), QStringLiteral("script-message app-shortcut-guide"));
    QVERIFY(menuLines >= 20);
}

void TestKeymap::shortcutHintsFollowBindings()
{
    ShortcutManager keys;
    keys.setDefaultBindings({
        QVariantMap{{"key", "p"}, {"command", "script-message app-toggle-playlist"}},
        QVariantMap{{"key", "Alt+a"}, {"command", "script-message app-audio-menu"}},
        QVariantMap{{"key", "PGDWN"}, {"command", "playlist-next"}},
        QVariantMap{{"key", ">"}, {"command", "playlist-next"}}
    });
    QCOMPARE(keys.commandKeys().value("playlist-next").toString(), "PGDWN");
    keys.applyOverrides({{"playlist-next", "p"}, {"script-message app-audio-menu", ""}});
    QCOMPARE(keys.commandKeys().value("playlist-next").toString(), "p");
    QVERIFY(keys.commandKeys().value("script-message app-toggle-playlist").toString().isEmpty());
    QVERIFY(keys.commandKeys().value("script-message app-audio-menu").toString().isEmpty());
    keys.applyOverrides({});
    QCOMPARE(keys.commandKeys().value("script-message app-audio-menu").toString(), "Alt+a");
}

void TestKeymap::focusedTextOwnsKeys()
{
    class FocusWindow final : public QWindow {
    public:
        QObject field;
        QObject *focusObject() const override { return const_cast<QObject *>(&field); }
    } window;
    ShortcutManager keys;
    keys.installFilter(&window);
    QSignalSpy shuttle(&keys, &ShortcutManager::shuttleStepUp);
    // A stale flag from another overlay must not turn a typed L into speed.
    window.field.setProperty("cursorPosition", 0);
    window.field.setProperty("readOnly", false);
    keys.setTextInputActive(false);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_L, Qt::NoModifier, "l");
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_L, Qt::NoModifier, "l");
    QCoreApplication::sendEvent(&window, &press);
    QCoreApplication::sendEvent(&window, &release);
    QCOMPARE(shuttle.size(), 0);
    window.field.setProperty("readOnly", true);
    QCoreApplication::sendEvent(&window, &press);
    QCoreApplication::sendEvent(&window, &release);
    QCOMPARE(shuttle.size(), 1);
}

QTEST_MAIN(TestKeymap)
#include "test_keymap.moc"
