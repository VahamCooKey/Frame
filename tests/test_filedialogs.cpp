#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QJSValue>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

class TestFileDialogs : public QObject {
    Q_OBJECT
private:
    static QObject *dialogWithProperty(QObject *root, const char *property)
    {
        for (QObject *child : root->findChildren<QObject *>()) {
            // Loader defers deletion of the previous dialog; read its
            // current item rather than accidentally selecting the old one.
            QObject *item = child->property("item").value<QObject *>();
            if (item && item->metaObject()->indexOfProperty(property) >= 0)
                return item;
        }
        return nullptr;
    }

    static QList<QUrl> urls(const QVariant &value)
    {
        const QVariant decoded = value.metaType() == QMetaType::fromType<QJSValue>()
            ? value.value<QJSValue>().toVariant() : value;
        QList<QUrl> result;
        for (const QVariant &entry : decoded.toList())
            result.append(entry.toUrl());
        return result;
    }

private slots:
    void acceptedFilesSurviveDialogUnload()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(FRAME_QML_DIR "/DeferredFileDialog.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QSignalSpy accepted(root.data(), SIGNAL(accepted(QVariant)));
        QVERIFY(accepted.isValid());
        QTemporaryDir directory;
        const QList<QUrl> selected{QUrl::fromLocalFile(directory.filePath("one space.mkv")),
                                  QUrl::fromLocalFile(directory.filePath("two.mp4"))};
        for (const auto &url : selected) {
            QFile file(url.toLocalFile());
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        // Loading the file dialog does not display it. Exercise the exact
        // accepted() signal Qt emits, including a second load after teardown.
        for (int attempt = 1; attempt <= 2; ++attempt) {
            root->setProperty("active", true);
            QObject *dialog = dialogWithProperty(root.data(), "selectedFiles");
            QVERIFY(dialog);
            QVERIFY(dialog->setProperty("fileMode", 1)); // OpenFiles
            // Qt 6.8 exposes the selectedFiles list as read-only; currentFiles
            // is its writable compatibility property for preselecting a batch.
            QVERIFY(dialog->setProperty("currentFiles", QVariant::fromValue(selected)));
            QCOMPARE(dialog->property("selectedFiles").value<QList<QUrl>>(), selected);
            QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
            QCOMPARE(accepted.count(), attempt);
            QCOMPARE(urls(accepted.last().first()), selected);
            QVERIFY(!root->property("active").toBool());
        }
    }

    void acceptedFolderSurvivesDialogUnload()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(FRAME_QML_DIR "/DeferredFileDialog.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QSignalSpy accepted(root.data(), SIGNAL(accepted(QVariant)));
        QTemporaryDir directory;
        const QUrl selected = QUrl::fromLocalFile(directory.path());
        root->setProperty("activeFolder", true);
        QObject *dialog = dialogWithProperty(root.data(), "selectedFolder");
        QVERIFY(dialog);
        QVERIFY(dialog->setProperty("selectedFolder", selected));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(urls(accepted.first().first()), QList<QUrl>{selected});
        QVERIFY(!root->property("activeFolder").toBool());
    }
};

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QGuiApplication app(argc, argv);
    TestFileDialogs test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_filedialogs.moc"
