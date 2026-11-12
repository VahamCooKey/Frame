#include <QtTest>
#include <QSignalSpy>
#include <QThread>
#include <QLocalSocket>
#include <QTimer>

#include <atomic>
#include <memory>
#include <vector>

#include "core/SingleInstance.h"

class TestSingleInstance : public QObject {
    Q_OBJECT

private slots:
    void limits_data();
    void limits();
    void lifecycle();
    void unterminatedRequest();
    void requestDeadline();
    void reentrantClose();
    void nobodyListening();
    void forwardsFilesToListener();
};

static QString uniqueName()
{
    return QStringLiteral("frame-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QRandomGenerator::global()->generate());
}

void TestSingleInstance::nobodyListening()
{
    // No server: the caller becomes the instance, and quickly.
    QElapsedTimer clock;
    clock.start();
    QVERIFY(!SingleInstance::forwardToRunning({QStringLiteral("a.mkv")}, true, uniqueName(), 500));
    QVERIFY(clock.elapsed() < 2000);
}

void TestSingleInstance::forwardsFilesToListener()
{
    const QString name = uniqueName();
    SingleInstance server;
    QVERIFY(server.listen(name));
    QSignalSpy spy(&server, &SingleInstance::openRequested);

    // The sender blocks for the acknowledgement without spinning an event
    // loop, so it must run on its own thread, like the second process does.
    std::atomic<bool> delivered{false};
    std::unique_ptr<QThread> sender(QThread::create([&] {
        delivered = SingleInstance::forwardToRunning(
            {QStringLiteral("C:/v/one two.mkv"), QStringLiteral("D:/clips")}, false, name, 2000);
    }));
    sender->start();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);
    QVERIFY(sender->wait(3000));
    QVERIFY(delivered.load());

    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toStringList(),
             QStringList({QStringLiteral("C:/v/one two.mkv"), QStringLiteral("D:/clips")}));
    QCOMPARE(args.at(1).toBool(), false);
}

void TestSingleInstance::limits_data()
{
    QTest::addColumn<int>("size");
    QTest::addColumn<bool>("chunked");
    QTest::addColumn<bool>("accepted");
    constexpr int limit = 4 * 1024 * 1024;
    for (bool chunks : {false, true}) {
        for (int delta : {-1, 0, 1})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(chunks).arg(delta)))
                << limit + delta << chunks << (delta <= 0);
    }
}

void TestSingleInstance::limits()
{
    QFETCH(int, size);
    QFETCH(bool, chunked);
    QFETCH(bool, accepted);
    SingleInstance server;
    const QString name = uniqueName();
    QVERIFY(server.listen(name));
    QSignalSpy spy(&server, &SingleInstance::openRequested);
    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(1000));
    QByteArray payload = "{\"files\":[],\"enqueue\":true}";
    payload.append(QByteArray(size - payload.size() - 1, ' '));
    payload.append('\n');
    if (chunked) {
        client.write(payload.left(size / 2));
        client.flush();
        QTest::qWait(50);
        QCOMPARE(spy.count(), 0);
        client.write(payload.mid(size / 2));
    } else {
        client.write(payload);
    }
    client.flush();
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QLocalSocket::UnconnectedState, 3000);
    QCOMPARE(spy.count(), accepted ? 1 : 0);
    QCOMPARE(client.readAll(), accepted ? QByteArray("ok\n") : QByteArray());
}

void TestSingleInstance::lifecycle()
{
    SingleInstance server;
    const QString name = uniqueName();
    QVERIFY(server.listen(name));
    QSignalSpy spy(&server, &SingleInstance::openRequested);
    // A live listener must never receive an oversized local handoff.
    QVERIFY(!SingleInstance::forwardToRunning({QString(4 * 1024 * 1024, QChar('x'))}, true, name, 100));
    auto connectClient = [&] {
        auto client = std::make_unique<QLocalSocket>();
        client->connectToServer(name);
        if (!client->waitForConnected(1000))
            return std::unique_ptr<QLocalSocket>();
        QCoreApplication::processEvents();
        return client;
    };
    auto pipeline = connectClient();
    QVERIFY(pipeline);
    connect(&server, &SingleInstance::openRequested, &server, [] {
        QCoreApplication::processEvents();
    });
    pipeline->write("{}\n{}\n");
    pipeline->flush();
    QTRY_COMPARE(spy.count(), 1);
    QTRY_VERIFY(pipeline->state() == QLocalSocket::UnconnectedState);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);
    std::vector<std::unique_ptr<QLocalSocket>> clients;
    for (int i = 0; i < 16; ++i) {
        clients.push_back(connectClient());
        QVERIFY(clients.back());
        clients.back()->write("{");
        clients.back()->flush();
    }
    auto excess = connectClient();
    QVERIFY(excess);
    QTRY_VERIFY(excess->state() == QLocalSocket::UnconnectedState);
    clients.front()->abort();
    QTest::qWait(50);
    auto reused = connectClient();
    QVERIFY(reused);
    reused->write("{}\n");
    reused->flush();
    QTRY_COMPARE(spy.count(), 2);
    auto closing = connectClient();
    QVERIFY(closing);
    closing->write("{");
    closing->flush();
    QTest::qWait(20);
    server.close();
    closing->write("}\n");
    closing->flush();
    QTRY_VERIFY(closing->state() == QLocalSocket::UnconnectedState);
    for (const auto &client : clients)
        QTRY_VERIFY(client->state() == QLocalSocket::UnconnectedState);
    QCOMPARE(spy.count(), 2);
}

void TestSingleInstance::unterminatedRequest()
{
    SingleInstance server;
    const QString name = uniqueName();
    QVERIFY(server.listen(name));
    QSignalSpy spy(&server, &SingleInstance::openRequested);
    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(1000));
    client.write(QByteArray(4 * 1024 * 1024, ' '));
    client.flush();
    // Reject when the buffer fills, without waiting for the lifetime limit.
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == QLocalSocket::UnconnectedState, 3000);
    QCOMPARE(spy.count(), 0);
    QVERIFY(client.readAll().isEmpty());
}

void TestSingleInstance::requestDeadline()
{
    SingleInstance server;
    const QString name = uniqueName();
    QVERIFY(server.listen(name));
    QSignalSpy spy(&server, &SingleInstance::openRequested);
    QLocalSocket idle, trickle;
    for (auto *client : {&idle, &trickle}) {
        client->connectToServer(name);
        QVERIFY(client->waitForConnected(1000));
    }
    QElapsedTimer clock;
    clock.start();
    QTimer writer;
    int writes = 0;
    connect(&writer, &QTimer::timeout, &trickle, [&] {
        if (trickle.state() == QLocalSocket::ConnectedState) {
            trickle.write(" ");
            trickle.flush();
            ++writes;
        }
    });
    writer.start(200);
    QTRY_VERIFY_WITH_TIMEOUT(idle.state() == QLocalSocket::UnconnectedState
                            && trickle.state() == QLocalSocket::UnconnectedState, 6500);
    QVERIFY(clock.elapsed() < 6500);
    QVERIFY(writes >= 4);
    QCOMPARE(spy.count(), 0);
    // Expiring requests must leave the listener usable.
    QLocalSocket next;
    next.connectToServer(name);
    QVERIFY(next.waitForConnected(1000));
    next.write("{}\n");
    next.flush();
    QTRY_COMPARE(spy.count(), 1);
    QTRY_VERIFY(next.state() == QLocalSocket::UnconnectedState);
    QCOMPARE(next.readAll(), QByteArray("ok\n"));
}

void TestSingleInstance::reentrantClose()
{
    SingleInstance server;
    const QString name = uniqueName();
    QVERIFY(server.listen(name));
    QSignalSpy spy(&server, &SingleInstance::openRequested);
    connect(&server, &SingleInstance::openRequested, &server, [&] {
        server.close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    });
    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(1000));
    client.write("{}\n{}\n");
    client.flush();
    QTRY_COMPARE(spy.count(), 1);
    QTRY_VERIFY(client.state() == QLocalSocket::UnconnectedState);
}

// No "second listener fails" case: Windows named pipes accept several
// servers on one name, which is why main() only listens after its own
// connection attempt found nobody.

QTEST_MAIN(TestSingleInstance)
#include "test_singleinstance.moc"
