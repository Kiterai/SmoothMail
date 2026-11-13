#include "mail/mailservice.h"
#include "mailtestserver.h"
#include <QSignalSpy>
#include <QFile>
#include <QSslConfiguration>
#include <QUuid>
#include <QtTest>
#include "storage/accountrepository.h"
#include "core/diagnostics.h"

using namespace smoothmail;
using ConnectionTester = MailService;
class ConnectionTesterTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setQuitLockEnabled(false);
        const auto output = qEnvironmentVariable("SMOOTHMAIL_GUI_OUTPUT");
        if (!output.isEmpty()) QVERIFY(diagnostics::start(output + QStringLiteral("/transport-diagnostics")));
    }
    void cleanupTestCase() { diagnostics::finish(); }
    void recentRefreshesAllFoldersAndHonorsExactBoundary()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true;
        const auto raw = [](const QByteArray &date) { return "From: fixture@example.test\r\nSubject: fixture\r\nDate: " + date + "\r\n\r\nbody"; };
        server.messages = {{1, raw("Tue, 10 Nov 2026 10:00:00 +0000")}, {2, raw("Fri, 13 Nov 2026 09:00:00 +0000")}, {3, raw("Fri, 13 Nov 2026 10:00:00 +0000")}};
        server.extraFolders.insert("Archive"); server.folderMessages["Archive"].insert(10, raw("Thu, 05 Nov 2026 10:00:00 +0000"));
        QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("recent@example.test"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        AccountRepository cache(QStringLiteral(":memory:")); QSignalSpy inserted(&cache, &AccountRepository::inserted), pages(&cache, &AccountRepository::pageLoaded);
        cache.insert(account); QTRY_COMPARE(inserted.size(), 1);
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); QSignalSpy result(&service, &MailService::synchronized);
        const auto sync = [&](MailService::SyncMode mode) { result.clear(); service.synchronize(account, QStringLiteral("dummy"), QStringLiteral("INBOX"), &cache, mode); };
        sync(MailService::SyncMode::All); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty()); QCOMPARE(server.fullFetchCount, 4);
        server.messageFlags[1] = {"\\Seen"}; server.messageFlags[2] = {"\\Seen"};
        server.messages.insert(4, raw("Fri, 13 Nov 2026 08:59:59 +0000"));
        server.messages.insert(5, raw("Thu, 12 Nov 2026 23:30:00 -1000")); // UTC 09:30; SENTSINCE ignores timezone.
        server.folderMessages["Archive"].insert(11, raw("Thu, 05 Nov 2026 09:30:00 +0000"));
        server.extraFolders.insert("New"); server.folderMessages["New"].insert(70, raw("Sun, 01 Nov 2026 00:00:00 +0000"));
        sync(MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString())); QCOMPARE(server.fullFetchCount, 7);
        MailQuery query; query.accountId = account.id; query.folder = QStringLiteral("INBOX"); query.sortColumn = 3;
        cache.loadPage(query); QTRY_COMPARE(pages.size(), 1); auto page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, 4);
        bool oldSeen = true, boundarySeen = false; for (const auto &message : page.messages) { QVERIFY(message.uid != 4); if (message.uid == 1) oldSeen = message.seen; if (message.uid == 2) boundarySeen = message.seen; }
        QVERIFY(!oldSeen); QVERIFY(boundarySeen);
        query.folder = QStringLiteral("Archive"); cache.loadPage(query); QTRY_COMPARE(pages.size(), 1); QCOMPARE(qvariant_cast<MailPage>(pages.takeFirst().first()).total, 2);
        query.folder = QStringLiteral("New"); cache.loadPage(query); QTRY_COMPARE(pages.size(), 1); QCOMPARE(qvariant_cast<MailPage>(pages.takeFirst().first()).total, 1);
        server.messages.remove(1); server.messages.remove(2);
        sync(MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        query.folder = QStringLiteral("INBOX"); cache.loadPage(query); QTRY_COMPARE(pages.size(), 1); page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, 3); // Retain old UID 1, remove window UID 2.
        sync(MailService::SyncMode::All); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        cache.loadPage(query); QTRY_COMPARE(pages.size(), 1); page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, 3);
        for (const auto &message : page.messages) QVERIFY(message.uid != 1 && message.uid != 2);
        server.uidValidity = 20;
        sync(MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        cache.loadPage(query); QTRY_COMPARE(pages.size(), 1); page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, 3); QCOMPARE(page.messages.first().uidValidity, 20);
    }
    void submitsQueuedFlagsInBatchesAndPrefersReadback()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true;
        server.messages = {{1, "Subject: one\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n\r\nbody"}, {2, "Subject: two\r\nDate: Fri, 13 Nov 2026 09:30:00 +0000\r\n\r\nbody"}}; QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("flags@example.test"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        AccountRepository cache(QStringLiteral(":memory:")); QSignalSpy inserted(&cache, &AccountRepository::inserted), changed(&cache, &AccountRepository::flagsUpdated), pages(&cache, &AccountRepository::pageLoaded);
        cache.insert(account); QTRY_COMPARE(inserted.size(), 1);
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); QSignalSpy result(&service, &MailService::synchronized);
        service.synchronize(account, QStringLiteral("dummy"), {}, &cache); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        const MailRecord first{account.id, QStringLiteral("INBOX"), 10, 1}, second{account.id, QStringLiteral("INBOX"), 10, 2};
        server.commands.clear(); cache.queueFlag(first, true, true); cache.queueFlag(second, true, true); cache.queueFlag(first, false, true); QTRY_COMPARE(changed.size(), 3);
        QVERIFY(server.commands.isEmpty());
        qint64 through = 0; QList<PendingFlag> pending; QVERIFY(cache.flagRevision(through).isEmpty()); QVERIFY(cache.pendingFlags(account.id, first.folder, 0, through, pending).isEmpty()); QCOMPARE(pending.size(), 3);
        server.rejectFlagChanges = true; result.clear(); service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(!result.first().at(4).toString().isEmpty());
        QVERIFY(cache.pendingFlags(account.id, first.folder, 0, through, pending).isEmpty()); QCOMPARE(pending.size(), 3);
        server.rejectFlagChanges = false; server.ignoreFlagWrites = true; server.commands.clear(); result.clear();
        service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString()));
        int stores = 0; for (const auto &command : server.commands) if (command.contains(" UID STORE ")) ++stores; QCOMPARE(stores, 2);
        QVERIFY(cache.pendingFlags(account.id, first.folder, 0, through, pending).isEmpty()); QVERIFY(pending.isEmpty());
        MailQuery query; query.accountId = account.id; query.folder = first.folder; cache.loadPage(query); QTRY_COMPARE(pages.size(), 1);
        const auto page = qvariant_cast<MailPage>(pages.takeFirst().first()); for (const auto &message : page.messages) { QVERIFY(!message.seen); QVERIFY(!message.important); }
        server.ignoreFlagWrites = false; cache.queueFlag(first, true, true); QTRY_COMPARE(changed.size(), 4); server.messages.remove(1); result.clear();
        service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        QVERIFY(cache.flagRevision(through).isEmpty()); QVERIFY(cache.pendingFlags(account.id, first.folder, 0, through, pending).isEmpty()); QVERIFY(pending.isEmpty());
    }
    void streamsMoreThanTenThousandAndReusesCache()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true;
        constexpr int count = 10034;
        for (int i = 1; i <= count; ++i) server.messages.insert(i * 2, "From: fixture@example.test\r\nSubject: " + QByteArray::number(i)
            + (i == 5901 ? QByteArray(" ") + QByteArray::fromHex("e697a5e69cace8aa9e")
                + "\r\nDate: " + QByteArray::fromHex("e5b9b4") : QByteArray{}) + "\r\n\r\nbatch body");
        QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("large@example.test"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        AccountRepository cache(QStringLiteral(":memory:")); QSignalSpy inserted(&cache, &AccountRepository::inserted);
        cache.insert(account); QTRY_COMPARE(inserted.size(), 1);
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        QSignalSpy result(&service, &MailService::synchronized), progress(&service, &MailService::syncProgress), pages(&cache, &AccountRepository::pageLoaded);
        service.synchronize(account, QStringLiteral("dummy"), {}, &cache);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 240000);
        QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString()));
        QVERIFY(qvariant_cast<QList<MailRecord>>(result.first().at(3)).isEmpty());
        QCOMPARE(server.fullFetchCount, count); QVERIFY(server.maximumHeaderWindow <= 100); QVERIFY(progress.size() > 100);
        MailQuery query; query.accountId = account.id; query.folder = QStringLiteral("INBOX"); query.page = 101;
        cache.loadPage(query); QTRY_COMPARE(pages.size(), 1);
        const auto page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, count); QCOMPARE(page.messages.size(), 34);
        // A failed refresh after several staged windows must preserve the old publication.
        result.clear(); server.messages.insert(999999, "From: new@example.test\r\nSubject: new\r\n\r\nnew body"); server.disconnectOnBodyFetch = true;
        service.synchronize(account, QStringLiteral("dummy"), {}, &cache); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 240000);
        QVERIFY(!result.first().at(4).toString().isEmpty());
        cache.loadPage(query); QTRY_COMPARE(pages.size(), 1); QCOMPARE(qvariant_cast<MailPage>(pages.takeFirst().first()).total, count);
        result.clear(); server.disconnectOnBodyFetch = false;
        service.synchronize(account, QStringLiteral("dummy"), {}, &cache); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 240000);
        QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString()));
        QCOMPARE(server.fullFetchCount, count + 1); // Existing bodies are not downloaded again.
        cache.loadPage(query); QTRY_COMPARE(pages.size(), 1); QCOMPARE(qvariant_cast<MailPage>(pages.takeFirst().first()).total, count + 1);
    }
    void streamsBeyondOldAggregateByteLimit()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true;
        const QByteArray raw = "From: fixture@example.test\r\nSubject: large body\r\nContent-Type: text/plain\r\n\r\n" + QByteArray(15 * 1024 * 1024, 'x');
        for (int i = 1; i <= 9; ++i) server.messages.insert(i, raw); // >135 MiB total, shared fixture bytes.
        QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("bytes@example.test"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        AccountRepository cache(QStringLiteral(":memory:")); QSignalSpy inserted(&cache, &AccountRepository::inserted);
        cache.insert(account); QTRY_COMPARE(inserted.size(), 1);
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); QSignalSpy result(&service, &MailService::synchronized);
        service.synchronize(account, QStringLiteral("dummy"), {}, &cache);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 240000);
        QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString()));
        QVERIFY(qvariant_cast<QList<MailRecord>>(result.first().at(3)).isEmpty());
        QSignalSpy pages(&cache, &AccountRepository::pageLoaded); MailQuery query; query.accountId = account.id; query.folder = QStringLiteral("INBOX");
        cache.loadPage(query); QTRY_COMPARE_WITH_TIMEOUT(pages.size(), 1, 15000);
        const auto page = qvariant_cast<MailPage>(pages.first().first()); QCOMPARE(page.total, 9);
        for (const auto &record : page.messages) { QVERIFY(record.document.rawSource.isEmpty()); QVERIFY(record.document.parts.isEmpty()); }
    }
    void folderOperationsAndServerErrors()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        server.provideMailbox = true; QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("test@example.com"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        QSignalSpy result(&service, &MailService::folderChanged);
        service.changeFolder(account, QStringLiteral("dummy"), QStringLiteral("create"), {}, QStringLiteral("Parent"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000); QVERIFY(result.takeFirst().at(1).toString().isEmpty());
        QVERIFY(server.extraFolders.contains("Parent"));
        service.changeFolder(account, QStringLiteral("dummy"), QStringLiteral("rename"), QStringLiteral("Parent"), QStringLiteral("Renamed"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000); QVERIFY(result.takeFirst().at(1).toString().isEmpty());
        QVERIFY(server.extraFolders.contains("Renamed")); QVERIFY(!server.extraFolders.contains("Parent"));
        server.rejectFolderChanges = true;
        service.changeFolder(account, QStringLiteral("dummy"), QStringLiteral("delete"), QStringLiteral("Renamed"), {});
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000); QVERIFY(!result.takeFirst().at(1).toString().isEmpty());
        QVERIFY(server.extraFolders.contains("Renamed"));
        server.rejectFolderChanges = false;
        service.changeFolder(account, QStringLiteral("dummy"), QStringLiteral("delete"), QStringLiteral("Renamed"), {});
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000); QVERIFY(result.takeFirst().at(1).toString().isEmpty());
        QVERIFY(server.extraFolders.isEmpty());
    }
    void mailboxSafetyLimitPreservesFailureSemantics()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        server.provideMailbox = true; server.advertisedMessageCount = 10001; QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("test@example.com"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        QSignalSpy result(&service, &MailService::synchronized);
        service.synchronize(account, QStringLiteral("dummy"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000);
        QVERIFY(result.first().at(4).toString().contains(QStringLiteral("10000")));
        QVERIFY(qvariant_cast<QList<MailRecord>>(result.first().at(3)).isEmpty());
        QVERIFY(!service.busy());
        result.clear();
        server.advertisedMessageCount = 1; server.advertisedMessageSize = 64 * 1024 * 1024 + 1;
        server.messages.insert(1, "From: fixture@example.test\r\nSubject: bounded\r\n\r\nbody");
        service.synchronize(account, QStringLiteral("dummy"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000);
        QVERIFY(result.first().at(4).toString().contains(QStringLiteral("MIME")));
        QVERIFY(qvariant_cast<QList<MailRecord>>(result.first().at(3)).isEmpty());
    }
    void disconnectDuringBodyFetchCanRetry()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        server.provideMailbox = true; server.disconnectOnBodyFetch = true;
        server.messages.insert(1, "From: fixture@example.test\r\nSubject: retry\r\n\r\nbody");
        QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("test@example.com"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        QSignalSpy result(&service, &MailService::synchronized);
        service.synchronize(account, QStringLiteral("dummy"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 20000);
        QVERIFY(!result.first().at(4).toString().isEmpty());
        QVERIFY(qvariant_cast<QList<MailRecord>>(result.first().at(3)).isEmpty());
        QVERIFY(!service.busy());
        result.clear(); server.disconnectOnBodyFetch = false;
        service.synchronize(account, QStringLiteral("dummy"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000);
        QVERIFY(result.first().at(4).toString().isEmpty());
        QCOMPARE(qvariant_cast<QList<MailRecord>>(result.first().at(3)).size(), 1);
    }
    void init() { m_defaultSsl = QSslConfiguration::defaultConfiguration(); }
    void cleanup() { QSslConfiguration::setDefaultConfiguration(m_defaultSsl); }
    void connection_data()
    {
        QTest::addColumn<int>("authentication");
        QTest::addColumn<bool>("startTls");
        QTest::addColumn<bool>("smtpOAuthMissing");
        QTest::newRow("PLAIN") << int(AuthenticationMode::Plain) << false << false;
        QTest::newRow("LOGIN") << int(AuthenticationMode::Login) << false << false;
        QTest::newRow("XOAUTH2") << int(AuthenticationMode::OAuth2) << false << false;
        QTest::newRow("STARTTLS-required") << int(AuthenticationMode::OAuth2) << true << false;
        QTest::newRow("no-oauth-fallback") << int(AuthenticationMode::OAuth2) << false << true;
    }
    void connection()
    {
        QFETCH(int, authentication);
        QFETCH(bool, startTls);
        QFETCH(bool, smtpOAuthMissing);
        const QStringList mechanisms{QStringLiteral("PLAIN"), QStringLiteral("LOGIN"), QStringLiteral("XOAUTH2")};
        MailTestServer smtp(MailTestServer::Protocol::Smtp, smtpOAuthMissing ? QStringList{QStringLiteral("PLAIN")} : mechanisms);
        MailTestServer imap(MailTestServer::Protocol::Imap, mechanisms);
        QVERIFY(smtp.listen());
        QVERIFY(imap.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("test@example.com"),
            {QStringLiteral("127.0.0.1"), smtp.port(), QStringLiteral("test@example.com"),
             static_cast<AuthenticationMode>(authentication), startTls ? EncryptionMode::StartTls : EncryptionMode::None},
            {QStringLiteral("127.0.0.1"), imap.port(), QStringLiteral("test@example.com"),
             static_cast<AuthenticationMode>(authentication), startTls ? EncryptionMode::StartTls : EncryptionMode::None}};
        ConnectionTester tester(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        QSignalSpy result(&tester, &ConnectionTester::finished);
        tester.test(account, QStringLiteral("dummy"), QStringLiteral("dummy"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000);
        const auto smtpError = result.first().at(0).toString();
        const auto imapError = result.first().at(1).toString();
        QCOMPARE(smtpError.isEmpty(), !startTls && !smtpOAuthMissing);
        QCOMPARE(imapError.isEmpty(), !startTls);
        QCOMPARE(smtp.receivedAuthentication(), !startTls && !smtpOAuthMissing);
        QCOMPARE(imap.receivedAuthentication(), !startTls);
    }
    void tls_data()
    {
        QTest::addColumn<bool>("startTls");
        QTest::addColumn<bool>("trusted");
        QTest::newRow("implicit-verified") << false << true;
        QTest::newRow("starttls-verified") << true << true;
        QTest::newRow("implicit-untrusted") << false << false;
        QTest::newRow("starttls-untrusted") << true << false;
    }
    void tls()
    {
        QFETCH(bool, startTls);
        QFETCH(bool, trusted);
        QFile certFile(QStringLiteral(":/test/tls-cert.pem"));
        QFile keyFile(QStringLiteral(":/test/tls-key.pem"));
        QVERIFY(certFile.open(QIODevice::ReadOnly));
        QVERIFY(keyFile.open(QIODevice::ReadOnly));
        const QSslCertificate certificate(certFile.readAll());
        const QSslKey key(keyFile.readAll(), QSsl::Rsa);
        QVERIFY(!certificate.isNull());
        QVERIFY(!key.isNull());
        if (trusted) {
            auto config = m_defaultSsl;
            config.addCaCertificate(certificate);
            QSslConfiguration::setDefaultConfiguration(config);
        }
        const QStringList mechanisms{QStringLiteral("PLAIN")};
        MailTestServer smtp(MailTestServer::Protocol::Smtp, mechanisms);
        MailTestServer imap(MailTestServer::Protocol::Imap, mechanisms);
        for (auto *server : {&smtp, &imap}) {
            server->tlsMode = startTls ? MailTestServer::TlsMode::StartTls : MailTestServer::TlsMode::Implicit;
            server->certificate = certificate;
            server->privateKey = key;
            QVERIFY(server->listen());
        }
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("test@example.com"),
            {QStringLiteral("127.0.0.1"), smtp.port(), QStringLiteral("test@example.com"), AuthenticationMode::Plain,
             startTls ? EncryptionMode::StartTls : EncryptionMode::ImplicitTls},
            {QStringLiteral("127.0.0.1"), imap.port(), QStringLiteral("test@example.com"), AuthenticationMode::Plain,
             startTls ? EncryptionMode::StartTls : EncryptionMode::ImplicitTls}};
        ConnectionTester tester(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        QSignalSpy result(&tester, &ConnectionTester::finished);
        tester.test(account, QStringLiteral("dummy"), QStringLiteral("dummy"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 40000);
        const auto smtpError = result.first().at(0).toString();
        const auto imapError = result.first().at(1).toString();
        QVERIFY2(smtpError.isEmpty() == trusted, qPrintable(smtpError));
        QVERIFY2(imapError.isEmpty() == trusted, qPrintable(imapError));
        QCOMPARE(smtp.receivedAuthentication(), trusted);
        QCOMPARE(imap.receivedAuthentication(), trusted);
        if (!trusted) {
            QVERIFY(smtp.authenticationResponses.isEmpty());
            QVERIFY(imap.authenticationResponses.isEmpty());
        }
    }
    void synchronizesMailboxAndUidValidity()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        server.provideMailbox = true;
        const QByteArray raw = "From: sender@example.test\r\nTo: test@example.com\r\nReply-To: reply@example.test\r\n"
            "Message-ID: <one@example.test>\r\nSubject: fixture\r\nDate: Thu, 12 Nov 2026 12:00:00 +0000\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n\r\nfixture body\r\n";
        server.messages.insert(1, raw);
        server.messages.insert(2, QByteArray(raw).replace("<one@", "<two@"));
        QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("test@example.com"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test@example.com")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test@example.com"), AuthenticationMode::Plain, EncryptionMode::None}};
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        QSignalSpy result(&service, &MailService::synchronized);
        service.synchronize(account, QStringLiteral("dummy"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 45000);
        QVERIFY(!service.busy()); // A completion observer can immediately start the next operation.
        QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString()));
        QCOMPARE(result.first().at(1).toString(), QStringLiteral("INBOX"));
        const auto folders = qvariant_cast<QList<MailFolder>>(result.first().at(2));
        QCOMPARE(folders.size(), 1);
        QCOMPARE(folders.first().unread, 2);
        const auto records = qvariant_cast<QList<MailRecord>>(result.first().at(3));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.first().uidValidity, 10);
        QCOMPARE(records.first().document.replyToAddresses, QStringList{QStringLiteral("reply@example.test")});
        QVERIFY(records.first().document.parts.first().text.contains(QStringLiteral("fixture body")));
        result.clear();
        server.uidValidity = 20;
        server.messages.clear();
        server.messages.insert(5, raw);
        service.synchronize(account, QStringLiteral("dummy"), QStringLiteral("INBOX"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 45000);
        QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString()));
        const auto updated = qvariant_cast<QList<MailRecord>>(result.first().at(3));
        QCOMPARE(updated.size(), 1);
        QCOMPARE(updated.first().uidValidity, 20);
        QCOMPARE(updated.first().uid, 5);
    }
    void postTlsCapabilitiesAreAuthoritative()
    {
        QFile certFile(QStringLiteral(":/test/tls-cert.pem")), keyFile(QStringLiteral(":/test/tls-key.pem"));
        QVERIFY(certFile.open(QIODevice::ReadOnly)); QVERIFY(keyFile.open(QIODevice::ReadOnly));
        const QSslCertificate certificate(certFile.readAll()); const QSslKey key(keyFile.readAll(), QSsl::Rsa);
        auto config = m_defaultSsl; config.addCaCertificate(certificate); QSslConfiguration::setDefaultConfiguration(config);
        MailTestServer smtp(MailTestServer::Protocol::Smtp, {QStringLiteral("XOAUTH2")});
        MailTestServer imap(MailTestServer::Protocol::Imap, {QStringLiteral("XOAUTH2")});
        for (auto *server : {&smtp, &imap}) {
            server->tlsMode = MailTestServer::TlsMode::StartTls;
            server->certificate = certificate; server->privateKey = key;
            server->changeMechanismsAfterTls = true; server->postTlsMechanisms = {QStringLiteral("PLAIN")};
            QVERIFY(server->listen());
        }
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("test@example.com"),
            {QStringLiteral("127.0.0.1"), smtp.port(), QStringLiteral("test"), AuthenticationMode::OAuth2, EncryptionMode::StartTls},
            {QStringLiteral("127.0.0.1"), imap.port(), QStringLiteral("test"), AuthenticationMode::OAuth2, EncryptionMode::StartTls}};
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        QSignalSpy result(&service, &MailService::finished);
        service.test(account, QStringLiteral("dummy-token"), QStringLiteral("dummy-token"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 45000);
        QVERIFY(smtp.tlsEstablished); QVERIFY(imap.tlsEstablished);
        QVERIFY(!result.first().at(0).toString().isEmpty()); QVERIFY(!result.first().at(1).toString().isEmpty());
        QVERIFY(!smtp.receivedAuthentication()); QVERIFY(!imap.receivedAuthentication());
        QVERIFY(smtp.authenticationResponses.isEmpty()); QVERIFY(imap.authenticationResponses.isEmpty());
    }

    void sendsAndSavesDraftWithoutLeakingBcc()
    {
        MailTestServer smtp(MailTestServer::Protocol::Smtp, {QStringLiteral("PLAIN")});
        MailTestServer imap(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        imap.provideMailbox = true; imap.provideDraftFolder = true;
        QVERIFY(smtp.listen()); QVERIFY(imap.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("test@example.com"),
            {QStringLiteral("127.0.0.1"), smtp.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None},
            {QStringLiteral("127.0.0.1"), imap.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        QSignalSpy result(&service, &MailService::submissionFinished);
        ComposeData draft;
        draft.from = account.email; draft.to = QStringLiteral("recipient@example.test");
        draft.bcc = QStringLiteral("hidden@example.test"); draft.subject = QStringLiteral("fixture send"); draft.body = QStringLiteral("dummy body");
        service.submit(account, QStringLiteral("dummy"), draft, false);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 45000);
        QVERIFY(!service.busy());
        QVERIFY2(result.first().at(2).toString().isEmpty(), qPrintable(result.first().at(2).toString()));
        QCOMPARE(smtp.deliveredMessages.size(), 1);
        QVERIFY(!smtp.deliveredMessages.first().contains("hidden@example.test"));
        QVERIFY(!smtp.deliveredMessages.first().toLower().contains("bcc:"));
        bool hiddenInEnvelope = false;
        for (const auto &recipient : smtp.envelopeRecipients) if (recipient.contains("hidden@example.test")) hiddenInEnvelope = true;
        QVERIFY(hiddenInEnvelope);
        result.clear();
        service.submit(account, QStringLiteral("dummy"), draft, true);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 45000);
        QVERIFY2(result.first().at(2).toString().isEmpty(), qPrintable(result.first().at(2).toString()));
        QCOMPARE(imap.savedDrafts.size(), 1);
        QVERIFY(imap.savedDrafts.first().contains("hidden@example.test"));
        result.clear();
        imap.provideDraftFolder = false;
        service.submit(account, QStringLiteral("dummy"), draft, true);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 45000);
        QVERIFY(!result.first().at(2).toString().isEmpty());
        QCOMPARE(imap.savedDrafts.size(), 1); // Never silently append drafts to INBOX.
    }
private:
    QSslConfiguration m_defaultSsl;
};
QTEST_GUILESS_MAIN(ConnectionTesterTest)
#include "tst_connectiontester.moc"
