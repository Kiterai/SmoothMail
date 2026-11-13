#include "core/diagnostics.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <QScopeGuard>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#include <crtdbg.h>
#include <cstdlib>
#endif
class DiagnosticsTest final : public QObject
{
    Q_OBJECT
    static QByteArray contents(const QDir &dir, const QString &pattern)
    {
        QByteArray output;
        for (const auto &name : dir.entryList({pattern}, QDir::Files)) {
            QFile file(dir.filePath(name)); if (file.open(QIODevice::ReadOnly)) output += file.readAll();
        }
        return output;
    }
private slots:
    void structuredFailuresAndWarningsContainOnlySafeMetadata()
    {
        using namespace smoothmail::diagnostics;
        QTemporaryDir temporary; QVERIFY(temporary.isValid()); QDir dir(temporary.path());
        QVERIFY(start(dir.path())); const auto finishSession = qScopeGuard([] { finish(); });
        const auto operation = nextOperation(), nested = nextOperation(); QVERIFY(nested > operation);
        {
            const OperationScope scope(operation);
            FolderValidationFailure failure; failure.total = 4; failure.emptyCount = 1; failure.duplicateCount = 2; failure.negativeUnreadCount = 1;
            failure.firstEmptyIndex = 1; failure.firstEmptyNoSelect = true; failure.firstDuplicateIndex = 3; failure.duplicateOfIndex = 2; failure.firstNegativeIndex = 4; failure.firstNegativeUnread = -7;
            folderValidationFailure(FolderWriteSource::SyncCommit, failure);
            { const OperationScope inner(nested); smoothmail::diagnostics::event("nested-test"); }
            smoothmail::diagnostics::event("restored-test");
            QTest::ignoreMessage(QtWarningMsg, "dummy-secret mail@example.test private-folder");
            QMessageLogger("private/path", 123, "private-function", "org.kde.pim.kimap").warning("dummy-secret mail@example.test private-folder");
            QTest::ignoreMessage(QtCriticalMsg, "dummy-secret");
            QMessageLogger("private/path", 456, "private-function", "private-category").critical("dummy-secret");
        }
        QCOMPARE(currentOperation(), OperationId(0));
        QScopedPointer<QThread> worker(QThread::create([operation] { const OperationScope scope(operation); smoothmail::diagnostics::event("cross-thread-test"); }));
        worker->start(); QVERIFY(worker->wait(5000));
        const auto log = contents(dir, QStringLiteral("*.log"));
        QVERIFY(log.contains("folder-metadata-invalid source=sync-commit total=4 empty_count=1 duplicate_count=2 negative_unread_count=1"));
        QVERIFY(log.contains("first_empty_index=1 first_empty_noselect=1 first_duplicate_index=3 duplicate_of_index=2 first_negative_index=4 first_negative_unread=-7 op=" + QByteArray::number(operation)));
        QVERIFY(log.contains("nested-test op=" + QByteArray::number(nested)));
        QVERIFY(log.contains("restored-test op=" + QByteArray::number(operation)));
        QVERIFY(log.contains("cross-thread-test op=" + QByteArray::number(operation)));
        QVERIFY(log.contains("qt-warning-kimap 123 op=" + QByteArray::number(operation)));
        QVERIFY(log.contains("qt-critical-other 456 op=" + QByteArray::number(operation)));
        for (const auto *secret : {"dummy-secret", "mail@example.test", "private-folder", "private/path", "private-function", "private-category"}) QVERIFY(!log.contains(secret));
    }
    void cleanExitAndKilledProcess()
    {
        QTemporaryDir temporary; QVERIFY(temporary.isValid()); QDir dir(temporary.path());
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), dir.path(), QStringLiteral("clean")});
        QVERIFY(child.waitForFinished()); QCOMPARE(child.exitCode(), 0);
        QCOMPARE(dir.entryList({QStringLiteral("*.active")}, QDir::Files).size(), 0);
        QVERIFY(contents(dir, QStringLiteral("*.log")).contains("session-clean-exit"));
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), dir.path(), QStringLiteral("wait")});
        QVERIFY(child.waitForReadyRead()); QCOMPARE(child.readAllStandardOutput().trimmed(), QByteArray("ready"));
        child.kill(); QVERIFY(child.waitForFinished());
        QCOMPARE(dir.entryList({QStringLiteral("*.active")}, QDir::Files).size(), 1);
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), dir.path(), QStringLiteral("clean")});
        QVERIFY(child.waitForFinished()); QCOMPARE(child.exitCode(), 0);
#ifdef Q_OS_WIN
        QCOMPARE(dir.entryList({QStringLiteral("*.active.abnormal")}, QDir::Files).size(), 1);
#endif
        QVERIFY(!contents(dir, QStringLiteral("*.log")).contains("dummy-secret"));
    }
    void fatalExceptions_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("windows-seh") << QStringLiteral("seh");
        QTest::newRow("std-terminate") << QStringLiteral("terminate");
        QTest::newRow("crt-assert") << QStringLiteral("crt");
        QTest::newRow("qt-fatal") << QStringLiteral("qt");
    }
    void fatalExceptions()
    {
        if (qEnvironmentVariable("SMOOTHMAIL_GUI_SANDBOX_TEST") != QStringLiteral("1")) QSKIP("Intentional crashes run only in disposable guest.");
        QFETCH(QString, mode);
        QTemporaryDir temporary; QVERIFY(temporary.isValid()); QDir dir(temporary.path());
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), dir.path(), mode});
        QVERIFY(child.waitForFinished(15000));
        QVERIFY(child.exitCode() != 0);
        const auto fatal = contents(dir, QStringLiteral("*.fatal.txt"));
        const auto expected = mode == QStringLiteral("seh") ? "code=0xc0000005" : mode == QStringLiteral("terminate") ? "std-terminate" : mode == QStringLiteral("crt") ? "crt-assert" : "qt-fatal";
        QVERIFY2(fatal.contains(expected), "Expected sanitized fatal marker was not recorded");
        QVERIFY(fatal.contains("offset=0x"));
        QVERIFY(!fatal.contains("dummy-secret"));
        QCOMPARE(dir.entryList({QStringLiteral("*.active")}, QDir::Files).size(), 1);
        const auto output = qEnvironmentVariable("SMOOTHMAIL_GUI_OUTPUT");
        if (!output.isEmpty()) {
            QDir target(output + QStringLiteral("/diagnostics-") + mode); QVERIFY(target.mkpath(QStringLiteral(".")));
            for (const auto &name : dir.entryList(QDir::Files)) QVERIFY(QFile::copy(dir.filePath(name), target.filePath(name)));
        }
    }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == QStringLiteral("--child")) {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
#endif
        if (!smoothmail::diagnostics::start(app.arguments().value(2))) return 3;
        smoothmail::diagnostics::event("test-stage", 42);
        const auto mode = app.arguments().value(3);
        if (mode == QStringLiteral("clean")) { smoothmail::diagnostics::finish(); return 0; }
#ifdef Q_OS_WIN
        if (mode == QStringLiteral("seh")) RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
        if (mode == QStringLiteral("terminate")) std::terminate();
#ifdef _MSC_VER
        if (mode == QStringLiteral("crt")) _ASSERTE(false && "dummy-secret");
#endif
        if (mode == QStringLiteral("qt")) qFatal("dummy-secret");
        fprintf(stdout, "ready\n"); fflush(stdout);
        return app.exec();
    }
    DiagnosticsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_diagnostics.moc"
