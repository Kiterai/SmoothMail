#include "mail/mimeparser.h"
#include "core/diagnostics.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class MimeRegressionTest final : public QObject
{
    Q_OBJECT
private slots:
    void preservesJapaneseAndEncodedWords()
    {
        const auto raw = QByteArray("From: =?UTF-8?B?5pel5pys6Kqe?= <fixture@example.test>\r\n"
            "Subject: =?UTF-8?B?5pel5pys6Kqe?= ") + QStringLiteral("日本語").toUtf8()
            + "\r\n\r\nbody";
        const auto parsed = smoothmail::MimeParser::parse(raw);
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.message.subject, QStringLiteral("日本語 日本語"));
        QCOMPARE(parsed.message.senderName, QStringLiteral("日本語"));
        QCOMPARE(parsed.message.rawSource, raw);
        const auto adjacent = smoothmail::MimeParser::parse(
            "Subject: =?UTF-8?B?5pel5pys6Kqe?= \t =?UTF-8?B?5pel5pys6Kqe?=\r\n\r\nbody");
        QVERIFY(adjacent.ok());
        QCOMPARE(adjacent.message.subject, QStringLiteral("日本語日本語"));
    }
    void highBytesInHeaders_data()
    {
        QTest::addColumn<QString>("field");
        QTest::newRow("date") << QStringLiteral("Date");
        QTest::newRow("control") << QStringLiteral("Control");
        QTest::newRow("newsgroups") << QStringLiteral("Newsgroups");
        QTest::newRow("uuencode-marker") << QStringLiteral("uuencode");
        QTest::newRow("yenc-marker") << QStringLiteral("yenc");
        QTest::newRow("subject") << QStringLiteral("Subject");
        QTest::newRow("from-display-name") << QStringLiteral("From");
        QTest::newRow("to-display-name") << QStringLiteral("To");
        QTest::newRow("subject-mixed-encoded-word") << QStringLiteral("SubjectMixed");
    }
    void highBytesInHeaders()
    {
        if (qEnvironmentVariable("SMOOTHMAIL_GUI_SANDBOX_TEST") != QStringLiteral("1")) QSKIP("Untrusted-parser crash regression runs only in disposable guest.");
        QFETCH(QString, field);
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), field, temporary.path()});
        QVERIFY(child.waitForFinished(30000));
        const auto output = qEnvironmentVariable("SMOOTHMAIL_GUI_OUTPUT");
        if (!output.isEmpty()) {
            QDir target(output + QStringLiteral("/mime-regression-") + field); QVERIFY(target.mkpath(QStringLiteral(".")));
            for (const auto &name : QDir(temporary.path()).entryList(QDir::Files)) QVERIFY(QFile::copy(QDir(temporary.path()).filePath(name), target.filePath(name)));
        }
        // Never include child stderr: parser diagnostics may echo input.
        QCOMPARE(child.exitStatus(), QProcess::NormalExit); QCOMPARE(child.exitCode(), 0);
    }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == QStringLiteral("--child")) {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
        if (!smoothmail::diagnostics::start(app.arguments().value(3))) return 3;
        const auto field = app.arguments().value(2).toLatin1();
        for (int byte = 0x80; byte <= 0xff; ++byte) {
            smoothmail::diagnostics::event("synthetic-header-byte", byte);
            auto raw = "From: fixture@example.test\r\n" + field + ": " + QByteArray(1, char(byte))
                + "\r\nSubject: synthetic regression\r\n\r\nbody";
            if (field == "uuencode") raw = "Content-Type: text/plain\r\n\r\nbegin " + QByteArray(1, char(byte)) + "44 fixture.txt\nM;F]T(')E86P@9&%T80``\n`\nend\n";
            if (field == "yenc") raw = "Content-Type: text/plain\r\n\r\n=ybegin line=" + QByteArray(1, char(byte)) + " size=1 name=fixture.txt\nx\n=yend size=1\n";
            if (field == "Subject" || field == "SubjectMixed") raw = "From: fixture@example.test\r\nSubject: "
                + (field == "SubjectMixed" ? QByteArray("=?UTF-8?B?5pel5pys6Kqe?= ") : QByteArray{})
                + QByteArray(1, char(byte)) + "\r\n\r\nbody";
            if (field == "From" || field == "To") raw = field + ": \"" + QByteArray(1, char(byte))
                + "\" <fixture@example.test>\r\nSubject: synthetic regression\r\n\r\nbody";
            if (!smoothmail::MimeParser::parse(raw).ok()) return 4;
        }
        smoothmail::diagnostics::finish(); return 0;
    }
    MimeRegressionTest test; return QTest::qExec(&test, argc, argv);
}
#include "tst_mimeregression.moc"
