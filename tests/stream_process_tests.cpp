#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QTest>

namespace {
struct PeerProcess : QProcess {
    ~PeerProcess()
    {
        if (state() != NotRunning) {
            kill();
            waitForFinished(5000);
        }
    }
};
}

class StreamProcessTests : public QObject
{
    Q_OBJECT
private slots:
    void streamsBetweenTwoProcesses()
    {
        const QString executable = QCoreApplication::applicationDirPath() + "/stream-process-peer";
        PeerProcess receiver;
        receiver.start(executable, {"receive"});
        QVERIFY(receiver.waitForStarted(5000));
        QTRY_VERIFY(receiver.canReadLine());
        QByteArray output = receiver.readLine();
        const auto match = QRegularExpression("LISTENING ([0-9]+)")
                               .match(QString::fromUtf8(output));
        QVERIFY2(match.hasMatch(), receiver.readAllStandardError().constData());
        PeerProcess sender;
        sender.start(executable, {"send", match.captured(1)});
        QVERIFY(sender.waitForStarted(5000));
        QTRY_COMPARE(receiver.state(), QProcess::NotRunning);
        output += receiver.readAllStandardOutput();
        QCOMPARE(receiver.exitStatus(), QProcess::NormalExit);
        QVERIFY2(receiver.exitCode() == 0, receiver.readAllStandardError().constData());
        QVERIFY(output.contains("FRAME_OK"));
        QTRY_COMPARE(sender.state(), QProcess::NotRunning);
        QCOMPARE(sender.exitStatus(), QProcess::NormalExit);
        QVERIFY2(sender.exitCode() == 0, sender.readAllStandardError().constData());
    }
};

QTEST_GUILESS_MAIN(StreamProcessTests)
#include "stream_process_tests.moc"
