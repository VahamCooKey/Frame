#include <QtTest>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSignalSpy>
#include "core/MpvRenderItem.h"

class TestRenderLifetime : public QObject {
    Q_OBJECT
private slots:
    void audioDeviceEnumerationSurvivesShutdown()
    {
        // Audio settings used to leave Qt's Windows COM/WinRT factories
        // invalid after libmpv teardown. The process must also exit cleanly
        // after this test (Qt releases those caches in its destructor).
        QQuickWindow window;
        window.resize(480, 360);
        window.show();
        QTimer::singleShot(50, &window, [&window] { window.hide(); window.show(); });
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        {
            PlayerController player;
            QTRY_VERIFY(!player.audioDevices().isEmpty());
            const auto devices = player.audioDevices();
            QVERIFY(!devices.isEmpty());
            QCOMPARE(devices.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("auto"));
        }
        window.update();
        QTest::qWait(100);
        QVERIFY(!window.grabWindow().isNull());
        window.hide();
    }

    void deleteVideoItemBeforeWindow_data()
    {
        QTest::addColumn<bool>("playing");
        QTest::newRow("idle") << false;
        QTest::newRow("playing") << true;
    }

    void deleteVideoItemBeforeWindow()
    {
        QFETCH(bool, playing);
        PlayerController player;
        player.command({"set", "audio", "no"});
        QQuickWindow window;
        window.resize(640, 360);
        window.show();
        // A hidden Windows process launch consumes its initial ShowWindow;
        // request exposure after startup messages have been delivered.
        QTimer::singleShot(50, &window, [&window] { window.hide(); window.show(); });
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (int cycle = 0; cycle < 4; ++cycle) {
            auto *item = new MpvRenderItem(window.contentItem());
            item->setSize(QSizeF(640, 360));
            QSignalSpy ready(item, &MpvRenderItem::renderContextReady);
            item->setPlayer(&player);
            QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 5000);
            if (playing) {
                player.openFile(QFileInfo("assets/test/sample.mkv").absoluteFilePath());
                QTRY_VERIFY_WITH_TIMEOUT(item->hasRenderedVideo() && player.position() > .05, 6000);
            }
            // Qt cleans up the scene graph after deleting the GUI item.
            // The render destructor and libmpv callbacks must not touch it.
            delete item;
            window.update();
            QTest::qWait(150);
            player.stop();
            QTRY_COMPARE(player.state(), PlayerController::State::Idle);
        }
        window.hide();
        window.destroy();
    }
};

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    TestRenderLifetime test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_render_lifetime.moc"
