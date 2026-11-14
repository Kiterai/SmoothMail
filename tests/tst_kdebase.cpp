#include <KLocalizedString>
#include <KService>
#include <KConfig>
#include <KConfigGroup>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class KdeBaseTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Test fallback text deterministically, regardless of the developer's locale.
        KLocalizedString::setApplicationDomain("smoothmail-dependency-test");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
    }

    void canFormatLocalizedText()
    {
        QCOMPARE(i18n("Mail for %1", QStringLiteral("test@example.com")),
                 QStringLiteral("Mail for test@example.com"));
        QCOMPARE(i18np("%1 item", "%1 items", 1), QStringLiteral("1 item"));
        QCOMPARE(i18np("%1 item", "%1 items", 2), QStringLiteral("2 items"));
    }

    void canConstructServiceMetadataWithoutExecuting()
    {
        // No desktop database lookup, process execution or registration.
        const KService service(QStringLiteral("SmoothMail dependency test"),
                               QStringLiteral("not-executed-command"), QStringLiteral("mail"));
        QCOMPARE(service.name(), QStringLiteral("SmoothMail dependency test"));
        QCOMPARE(service.exec(), QStringLiteral("not-executed-command"));
    }

    void configurationMigrationIsExplicitlyDisabled()
    {
#ifndef Q_OS_WIN
        QSKIP("The no-update KConfig patch is enabled for Windows builds only.");
#else
        // Refuse to invoke this API on an unexpected DLL: the upstream
        // implementation can launch a process and touch non-fixture settings.
        const auto module = GetModuleHandleW(L"KF6ConfigCore.dll");
        QVERIFY(module != nullptr);
        wchar_t modulePath[32768] = {};
        const auto length = GetModuleFileNameW(module, modulePath, 32768);
        QVERIFY(length > 0 && length < 32768);
        QFile library(QString::fromWCharArray(modulePath, int(length)));
        QVERIFY(library.open(QIODevice::ReadOnly));
        const auto binary = library.readAll();
        QCOMPARE(QCryptographicHash::hash(binary, QCryptographicHash::Sha256).toHex(),
                 QByteArray(SMOOTHMAIL_EXPECTED_KCONFIG_SHA256));
        QVERIFY(binary.contains("KConfig::checkUpdate is disabled in this build."));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("fixture.ini")));
        const QByteArray original("[Fixture]\nvalue=before\n[$Version]\nupdate_info=existing:done\n");
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        KConfig config(file.fileName(), KConfig::SimpleConfig);
        KConfigGroup group(&config, QStringLiteral("Fixture"));
        QCOMPARE(group.readEntry("value", QString()), QStringLiteral("before"));
        group.writeEntry("value", QStringLiteral("pending"));
        QVERIFY(config.isDirty());

        QTest::ignoreMessage(QtWarningMsg, "KConfig::checkUpdate is disabled in this build.");
        config.checkUpdate(QStringLiteral("synthetic-private-id"), QStringLiteral("synthetic-private-file.upd"));
        // No implicit reparse, sync, or invented migration-complete marker.
        QVERIFY(config.isDirty());
        QCOMPARE(group.readEntry("value", QString()), QStringLiteral("pending"));
        QCOMPARE(KConfigGroup(&config, QStringLiteral("$Version")).readEntry("update_info", QStringList()),
                 QStringList{QStringLiteral("existing:done")});
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
        config.markAsClean(); // Do not save the intentionally pending fixture.
#endif
    }
};

QTEST_GUILESS_MAIN(KdeBaseTest)
#include "tst_kdebase.moc"
