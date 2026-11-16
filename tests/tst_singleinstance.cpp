#include "core/singleinstance.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>

class SingleInstanceTest final : public QObject {
    Q_OBJECT
private slots:
    void excludesOtherProcessAndReleases()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto runChild = [&] {
            QProcess child;
            child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--lock-child"), directory.path()});
            if (!child.waitForFinished(10000)) return -1;
            return child.exitStatus() == QProcess::NormalExit ? child.exitCode() : -1;
        };
        {
            smoothmail::SingleInstance instance(directory.path());
            QVERIFY(instance.acquire());
            QCOMPARE(runChild(), 7);
        }
        QCOMPARE(runChild(), 0);
    }
    void recoversDeadProcess()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hold-lock"), directory.path()});
        QVERIFY(child.waitForReadyRead(10000));
        QCOMPARE(child.readAllStandardOutput().trimmed(), QByteArray("locked"));
        {
            smoothmail::SingleInstance blocked(directory.path());
            QVERIFY(!blocked.acquire()); QVERIFY(blocked.alreadyRunning());
        }
        child.kill(); QVERIFY(child.waitForFinished(10000));
        smoothmail::SingleInstance recovered(directory.path());
        QVERIFY(recovered.acquire());
    }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == QStringLiteral("--lock-child")) {
        smoothmail::SingleInstance instance(app.arguments()[2]);
        return instance.acquire() ? 0 : instance.alreadyRunning() ? 7 : 8;
    }
    if (app.arguments().size() == 3 && app.arguments()[1] == QStringLiteral("--hold-lock")) {
        smoothmail::SingleInstance instance(app.arguments()[2]);
        if (!instance.acquire()) return 8;
        std::puts("locked"); std::fflush(stdout);
        return app.exec();
    }
    SingleInstanceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_singleinstance.moc"
