// Requires a windowing session; gate with FRAME_ENABLE_RENDER_TESTS.
#include "platform/NativeWindowLinux.h"

#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

class TestNativeDesktopLinux final : public QObject {
    Q_OBJECT
private slots:
    void topmostPreferenceSurvivesStates()
    {
        QQuickWindow window;
        NativeWindowLinux native;
        window.setFlags(Qt::Window | Qt::FramelessWindowHint);
        window.setGeometry(100, 100, 640, 360);
        native.attach(&window);
        QSignalSpy changed(&native, &INativeWindow::alwaysOnTopChanged);
        native.setAlwaysOnTop(true);
        window.show();
        QVERIFY(window.flags().testFlag(Qt::WindowStaysOnTopHint));
        window.showMaximized();
        QTRY_VERIFY(window.windowStates().testFlag(Qt::WindowMaximized));
        native.setFullscreenWindow(true, 100, 100, 640, 360);
        native.setFullscreenWindow(false, 100, 100, 640, 360);
        QTRY_VERIFY(window.windowStates().testFlag(Qt::WindowMaximized));
        QVERIFY(native.alwaysOnTop());
        native.setAlwaysOnTop(false);
        window.showNormal();
        native.setFullscreenWindow(true, 100, 100, 640, 360);
        native.setFullscreenWindow(false, 100, 100, 640, 360);
        QVERIFY(!window.flags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(window.flags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(changed.count(), 2);
    }
    void destroyedWindowIsSafe()
    {
        NativeWindowLinux native;
        auto *window = new QQuickWindow;
        native.attach(window);
        delete window;
        native.toggleAlwaysOnTop();
        QVERIFY(native.alwaysOnTop());
        native.raiseToFront();
    }
};
QTEST_MAIN(TestNativeDesktopLinux)
#include "test_nativedesktop_linux.moc"
