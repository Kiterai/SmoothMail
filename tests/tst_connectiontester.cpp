#include "mail/mailservice.h"
#include "mailtestserver.h"
#include <QSignalSpy>
#include <QFile>
#include <QSslConfiguration>
#include <QUuid>
#include <QtTest>
#include <QElapsedTimer>
#include "storage/accountrepository.h"
#include "core/diagnostics.h"
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QDir>
#include <QRegularExpression>

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
    void folderRefreshStaysScopedAndDefersCrossFolderOperations_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("recent") << 0; QTest::newRow("full") << 1;
        QTest::newRow("unsubmitted-folder") << 2;
        QTest::newRow("renamed-recent") << 3; QTest::newRow("renamed-full") << 4;
    }
    void folderRefreshStaysScopedAndDefersCrossFolderOperations()
    {
        QFETCH(int, kind);
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true; server.extraFolders.insert("Other");
        const auto raw = [](const QByteArray &subject, const QByteArray &date) { return "Subject: " + subject + "\r\nDate: " + date + "\r\n\r\nsynthetic body"; };
        server.messages = {{1, raw("old", "Sun, 01 Nov 2026 10:00:00 +0000")}, {2, raw("latest", "Fri, 13 Nov 2026 10:00:00 +0000")}};
        server.folderMessages["Other"].insert(10, raw("other", "Fri, 13 Nov 2026 10:00:00 +0000")); QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("scoped@example.test"), {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")}, {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        AccountRepository cache(QStringLiteral(":memory:")); QSignalSpy inserted(&cache, &AccountRepository::inserted), queued(&cache, &AccountRepository::folderQueued), flags(&cache, &AccountRepository::flagsUpdated), pages(&cache, &AccountRepository::pageLoaded), cached(&cache, &AccountRepository::cacheLoaded), local(&cache, &AccountRepository::localOperationFinished);
        cache.insert(account); QTRY_COMPARE(inserted.size(), 1); MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); QSignalSpy result(&service, &MailService::synchronized);
        service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::All); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        cache.queueFolder(account.id, QStringLiteral("create"), {}, QStringLiteral("Pending")); QTRY_COMPARE(queued.size(), 1);
        const bool renamed = kind >= 3; const auto target = renamed ? QStringLiteral("RenamedInbox") : QStringLiteral("INBOX");
        if (renamed) { cache.queueFolder(account.id, QStringLiteral("rename"), QStringLiteral("INBOX"), target); QTRY_COMPARE(queued.size(), 2); }
        const MailRecord other{account.id, QStringLiteral("Other"), 10, 10}; cache.queueFlag(other, true, true); QTRY_COMPARE(flags.size(), 1);
        server.messages.remove(1); server.messages.insert(3, raw("new", "Fri, 13 Nov 2026 10:30:00 +0000")); server.folderMessages["Other"].insert(11, raw("unfetched", "Fri, 13 Nov 2026 11:00:00 +0000"));
        const auto mode = kind == 1 || kind == 4 ? MailService::SyncMode::SelectedFolder : MailService::SyncMode::SelectedRecent;
        server.commands.clear(); result.clear(); service.synchronize(account, QStringLiteral("dummy"), kind == 2 ? QStringLiteral("Pending") : target, &cache, mode);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        if (kind == 2) { QVERIFY(result.first().at(4).toString().contains(QStringLiteral("未作成"))); QVERIFY(server.commands.isEmpty()); return; }
        QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString()));
        for (const auto &command : server.commands) {
            QVERIFY(!command.contains("\"Other\"")); QVERIFY(!command.contains(" CREATE ") && !command.contains(" RENAME ") && !command.contains(" UID STORE "));
        }
        MailQuery query; query.accountId = account.id; query.folder = target; cache.loadPage(query); QTRY_COMPARE(pages.size(), 1);
        QCOMPARE(qvariant_cast<MailPage>(pages.takeFirst().first()).total, mode == MailService::SyncMode::SelectedRecent ? 3 : 2);
        query.folder = QStringLiteral("Other"); cache.loadPage(query); QTRY_COMPARE(pages.size(), 1); const auto page = qvariant_cast<MailPage>(pages.takeFirst().first());
        QCOMPARE(page.total, 1); QVERIFY(page.messages.first().important); QVERIFY(!server.folderFlags["Other"][10].contains("\\Flagged"));
        cache.loadCache(account.id, true); QTRY_COMPARE(cached.size(), 1); bool otherFound = false;
        for (const auto &folder : qvariant_cast<QList<MailFolder>>(cached.first().at(2))) if (folder.name == QStringLiteral("Other")) { otherFound = true; QCOMPARE(folder.unread, 1); } QVERIFY(otherFound);
        qint64 through = 0; QList<PendingFolder> pending; QVERIFY(cache.folderRevision(account.id, through).isEmpty()); QVERIFY(cache.pendingFolders(account.id, 0, through, pending).isEmpty()); QCOMPARE(pending.size(), renamed ? 2 : 1);
        cache.setMoveIntent(QStringLiteral("deferred-move"), other, target); QTRY_COMPARE(local.size(), 1); QVERIFY(local.first().at(1).toString().isEmpty());
        server.commands.clear(); result.clear(); service.synchronize(account, QStringLiteral("dummy"), target, &cache, mode); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        for (const auto &command : server.commands) QVERIFY(!command.contains("\"Other\"") && !command.contains(" UID MOVE ") && !command.contains(" CREATE ") && !command.contains(" RENAME "));
        QList<PendingMove> moves; QVERIFY(cache.moveRevision(account.id, through).isEmpty()); QVERIFY(cache.pendingMoves(account.id, 0, through, moves).isEmpty()); QCOMPARE(moves.size(), 1);
    }
    void duplicateFolderListingIsMergedBeforeFetching_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("same-batch") << 0;
        QTest::newRow("response-16-duplicates-2-across-batches") << 1;
        QTest::newRow("placeholder-before-selectable") << 2;
        QTest::newRow("selectable-before-placeholder") << 3;
        QTest::newRow("both-noselect") << 4;
        QTest::newRow("separator-conflict") << 5;
        QTest::newRow("non-inbox-case-distinct") << 6;
        QTest::newRow("raw-response-limit") << 7;
        QTest::newRow("inbox-case-normalized") << 8;
    }
    void duplicateFolderListingIsMergedBeforeFetching()
    {
        QFETCH(int, kind);
        QTemporaryDir temporary; QVERIFY(temporary.isValid()); const auto output = qEnvironmentVariable("SMOOTHMAIL_GUI_OUTPUT");
        const auto directory = output.isEmpty() ? temporary.path() : output + QStringLiteral("/transport-diagnostics");
        if (output.isEmpty()) QVERIFY(diagnostics::start(directory));
        const auto finishSession = qScopeGuard([&] { if (output.isEmpty()) diagnostics::finish(); });
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true; server.duplicateInboxListing = true; QVERIFY(server.listen());
        const QByteArray inbox("* LIST (\\HasNoChildren) \"/\" \"INBOX\"");
        const QByteArray placeholder("* LIST (\\Noselect \\HasChildren \\X-Placeholder) \"/\" \"INBOX\"");
        const QByteArray selectable("* LIST (\\HasNoChildren \\Noinferiors \\Marked \\X-Selectable) \"/\" \"INBOX\"");
        if (kind == 1) {
            server.listResponses = {"* LIST (\\Noselect) \"/\" \"fixture-1\"", inbox};
            for (int i = 3; i <= 15; ++i) server.listResponses.append("* LIST (\\Noselect) \"/\" \"fixture-" + QByteArray::number(i) + '"');
            server.listTailResponses = {inbox}; server.listTailDelayMs = 300;
        }
        if (kind == 2) server.listResponses = {placeholder, selectable};
        if (kind == 3) server.listResponses = {selectable, placeholder};
        if (kind == 4) server.listResponses = {placeholder, "* LIST (\\Noselect \\HasNoChildren) \"/\" \"INBOX\""};
        if (kind == 5) server.listResponses = {inbox, "* LIST () \".\" \"INBOX\""};
        if (kind == 6) server.listResponses = {inbox, "* LIST () \"/\" \"Archive\"", "* LIST () \"/\" \"archive\""};
        if (kind == 7) for (int i = 0; i < 10001; ++i) server.listResponses.append(inbox);
        if (kind == 8) server.listResponses = {"* LIST () \"/\" \"inbox\"", inbox};
        server.messages.insert(7, "Subject: synthetic fixture\r\n\r\nsynthetic body");
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("private-account@example.test"), {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")}, {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        AccountRepository cache(QStringLiteral(":memory:")); QSignalSpy inserted(&cache, &AccountRepository::inserted);
        cache.insert(account); QTRY_COMPARE(inserted.size(), 1);
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); QSignalSpy result(&service, &MailService::synchronized);
        service.synchronize(account, QStringLiteral("dummy-private-secret"), {}, &cache, MailService::SyncMode::All);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QByteArray log;
        for (const auto &name : QDir(directory).entryList({QStringLiteral("*.log")}, QDir::Files)) { QFile file(QDir(directory).filePath(name)); QVERIFY(file.open(QIODevice::ReadOnly)); log += file.readAll(); }
        auto starts = QRegularExpression(QStringLiteral("sync-start op=(\\d+)")).globalMatch(QString::fromUtf8(log));
        QString operation; while (starts.hasNext()) operation = starts.next().captured(1);
        QVERIFY(!operation.isEmpty()); const auto suffix = " op=" + operation.toUtf8();
        for (const auto &secret : {account.id.toUtf8(), account.email.toUtf8(), QByteArray("dummy-private-secret"), QByteArray("INBOX"), QByteArray("127.0.0.1")}) QVERIFY(!log.contains(secret));
        if (kind == 5 || kind == 7) {
            QCOMPARE(result.first().at(4).toString(), kind == 5 ? QStringLiteral("同名フォルダの階層区切りが一致しません。") : QStringLiteral("フォルダ数が安全上限を超えています。"));
            QVERIFY(log.contains((kind == 5 ? "sync-list-separator-conflict 2" : "sync-list-raw-total 10001") + suffix));
            QCOMPARE(server.fullFetchCount, 0);
            for (const auto &command : server.commands) QVERIFY(!command.contains(" STATUS ") && !command.contains(" SELECT ") && !command.contains(" EXAMINE "));
            server.duplicateInboxListing = false; server.listResponses.clear(); result.clear();
            service.synchronize(account, QStringLiteral("dummy-private-secret"), {}, &cache, MailService::SyncMode::All);
            QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty()); QCOMPARE(server.fullFetchCount, 1);
            return;
        }
        QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString()));
        const int total = kind == 1 ? 15 : kind == 6 ? 3 : 1;
        const auto folders = qvariant_cast<QList<MailFolder>>(result.first().at(2)); QCOMPARE(folders.size(), total);
        QVERIFY(log.contains("sync-list-complete " + QByteArray::number(total) + suffix));
        if (kind != 6) {
            QVERIFY(log.contains("sync-list-duplicates-merged 1" + suffix));
            QVERIFY(log.contains("sync-list-first-duplicate-index " + QByteArray::number(kind == 1 ? 16 : 2) + suffix));
            QVERIFY(log.contains("sync-list-duplicate-of-index " + QByteArray::number(kind == 1 ? 2 : 1) + suffix));
        }
        QVERIFY(log.contains("sync-complete 0" + suffix)); QVERIFY(!log.contains("sync-failed 0" + suffix));
        int statuses = 0, selections = 0;
        for (const auto &command : server.commands) {
            if (command.contains(" STATUS \"INBOX\" ")) ++statuses;
            if (command.contains(" EXAMINE \"INBOX\"") || command.contains(" SELECT \"INBOX\"")) ++selections;
        }
        QCOMPARE(statuses, kind == 4 ? 0 : 1);
        // One initial SELECT/EXAMINE and one final UIDVALIDITY/count/UIDNEXT
        // verification; duplicate LIST rows must not start a second such pair.
        QCOMPARE(selections, kind == 4 ? 0 : 2);
        if (kind == 1) {
            int batches = 0;
            for (const auto &line : log.split('\n')) if (line.contains(" sync-list-batch ") && line.endsWith(suffix)) ++batches;
            QVERIFY(batches >= 2); // KIMAP's 100 ms emission timer must have split this response.
        }
        if (kind == 2 || kind == 3 || kind == 4) {
            QSet<QByteArray> flags; for (const auto &flag : folders.first().flags) flags.insert(flag.toUpper());
            QCOMPARE(flags.contains("\\NOSELECT"), kind == 4); QVERIFY(flags.contains("\\HASCHILDREN")); QVERIFY(!flags.contains("\\HASNOCHILDREN")); QVERIFY(!flags.contains("\\NOINFERIORS"));
            QVERIFY(flags.contains("\\X-PLACEHOLDER"));
            if (kind != 4) { QVERIFY(flags.contains("\\MARKED")); QVERIFY(flags.contains("\\X-SELECTABLE")); }
        }
        QSignalSpy pages(&cache, &AccountRepository::pageLoaded); MailQuery query; query.accountId = account.id; query.folder = QStringLiteral("INBOX");
        cache.loadPage(query); QTRY_COMPARE(pages.size(), 1);
        const auto page = qvariant_cast<MailPage>(pages.first().first()); QVERIFY(page.error.isEmpty()); QCOMPARE(page.total, kind == 4 ? 0 : 1);
        QCOMPARE(server.fullFetchCount, kind == 4 ? 0 : 1);
        if (kind == 0) {
            QSignalSpy queued(&cache, &AccountRepository::folderQueued);
            cache.queueFolder(account.id, QStringLiteral("create"), {}, QStringLiteral("Created")); QTRY_COMPARE(queued.size(), 1); QVERIFY(queued.first().at(1).toString().isEmpty());
            result.clear(); service.synchronize(account, QStringLiteral("dummy-private-secret"), {}, &cache, MailService::SyncMode::All);
            QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty()); QVERIFY(server.extraFolders.contains("Created"));
        }
        server.duplicateInboxListing = false; result.clear();
        server.listResponses.clear(); server.listTailResponses.clear(); server.listTailDelayMs = 0;
        service.synchronize(account, QStringLiteral("dummy-private-secret"), {}, &cache, MailService::SyncMode::All);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
    }
    void nativeMoveRemapsFlagsAndSupportsReverseMove_data()
    {
        QTest::addColumn<bool>("supported"); QTest::addColumn<bool>("mapping");
        QTest::newRow("mapped") << true << true;
        QTest::newRow("unsupported") << false << true;
        QTest::newRow("lost-mapping") << true << false;
    }
    void nativeMoveRemapsFlagsAndSupportsReverseMove()
    {
        QFETCH(bool, supported); QFETCH(bool, mapping);
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true; server.supportMove = supported; server.omitMoveMapping = !mapping;
        server.extraFolders.insert("Target"); server.messages.insert(7, "Subject: move fixture\r\n\r\nbody"); server.folderMessages["Target"].insert(90, "Subject: existing\r\n\r\nbody"); QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("move@example.test"), {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")}, {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        AccountRepository cache(QStringLiteral(":memory:")); QSignalSpy inserted(&cache, &AccountRepository::inserted), changed(&cache, &AccountRepository::localOperationFinished), flags(&cache, &AccountRepository::flagsUpdated);
        cache.insert(account); QTRY_COMPARE(inserted.size(), 1);
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); QSignalSpy result(&service, &MailService::synchronized);
        const auto sync = [&] { result.clear(); service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::All); };
        sync(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        const MailRecord identity{account.id, QStringLiteral("INBOX"), 10, 7};
        cache.setFlagIntent(QStringLiteral("flag"), identity, true, true); QTRY_COMPARE(flags.size(), 1);
        cache.setMoveIntent(QStringLiteral("move"), identity, QStringLiteral("Target")); QTRY_COMPARE(changed.size(), 1); QVERIFY(changed.first().at(1).toString().isEmpty());
        server.commands.clear(); sync(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        if (!supported) {
            QVERIFY(!result.first().at(4).toString().isEmpty()); QVERIFY(server.messages.contains(7));
            for (const auto &command : server.commands) QVERIFY(!command.contains(" UID MOVE ") && !command.contains(" UID COPY ") && !command.contains("EXPUNGE"));
            return;
        }
        QVERIFY(!server.messages.contains(7)); QVERIFY(server.folderMessages["Target"].contains(91));
        if (!mapping) {
            QVERIFY(!result.first().at(4).toString().isEmpty()); server.commands.clear(); sync(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
            for (const auto &command : server.commands) QVERIFY(!command.contains(" UID MOVE "));
            return;
        }
        QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString())); QVERIFY(server.folderFlags["Target"][91].contains("\\Flagged"));
        cache.setMoveIntent(QStringLiteral("move"), identity, QStringLiteral("INBOX")); QTRY_COMPARE(changed.size(), 2); QVERIFY(changed.last().at(1).toString().isEmpty());
        sync(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString())); QVERIFY(!server.folderMessages["Target"].contains(91)); QVERIFY(server.messages.contains(8));
        cache.setFlagIntent(QStringLiteral("flag"), identity, true, false); QTRY_COMPARE(flags.size(), 2); QVERIFY(flags.last().at(1).toString().isEmpty());
        sync(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty()); QVERIFY(!server.messageFlags[8].contains("\\Flagged"));
    }
    void cancelsStalledSynchronizationAndCanRetry()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true; server.pauseHeaders = true;
        server.messages.insert(1, "Subject: fixture\r\n\r\nbody"); QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("cancel@example.test"), {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); QSignalSpy result(&service, &MailService::synchronized);
        service.synchronize(account, QStringLiteral("dummy")); QTRY_COMPARE_WITH_TIMEOUT(server.pausedHeaderCount(), 1, 10000);
        service.cancelSynchronization(QStringLiteral("another-account")); QVERIFY(service.busy());
        QElapsedTimer elapsed; elapsed.start(); service.cancelSynchronization(account.id);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 3000); QVERIFY(elapsed.elapsed() < 3000); QVERIFY(!result.first().at(4).toString().isEmpty()); QCOMPARE(server.fullFetchCount, 0);
        server.pauseHeaders = false; result.clear(); service.synchronize(account, QStringLiteral("dummy")); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
    }
    void folderCreationUndoRedoUsesOrderedInversesAndRefusesNonemptyDelete()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true; QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("create@example.test"), {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")}, {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        AccountRepository cache(QStringLiteral(":memory:")); QSignalSpy inserted(&cache, &AccountRepository::inserted), queued(&cache, &AccountRepository::folderQueued); cache.insert(account); QTRY_COMPARE(inserted.size(), 1);
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); QSignalSpy result(&service, &MailService::synchronized);
        const auto sync = [&] { result.clear(); service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::All); };
        sync(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        cache.restoreCreatedFolder(account.id, QStringLiteral("Created")); QTRY_COMPARE(queued.size(), 1); cache.cancelCreatedFolder(account.id, QStringLiteral("Created")); QTRY_COMPARE(queued.size(), 2); cache.restoreCreatedFolder(account.id, QStringLiteral("Created")); QTRY_COMPARE(queued.size(), 3);
        for (const auto &entry : queued) QVERIFY(entry.at(1).toString().isEmpty());
        sync(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString())); QVERIFY(server.extraFolders.contains("Created"));
        // A message arriving after the last cache fetch must not be deleted by undo.
        server.folderMessages["Created"].insert(1, "Subject: newly arrived\r\n\r\nbody"); cache.cancelCreatedFolder(account.id, QStringLiteral("Created")); QTRY_COMPARE(queued.size(), 4);
        sync(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(!result.first().at(4).toString().isEmpty()); QVERIFY(server.extraFolders.contains("Created")); QCOMPARE(server.folderMessages["Created"].size(), 1);
    }
    void synchronizesDeferredFolderOperationsBeforeFlags()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true; server.extraFolders.insert("Archive");
        server.folderMessages["Archive"].insert(7, "Subject: retained\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n\r\nbody"); QVERIFY(server.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("folders@example.test"), {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        AccountRepository cache(QStringLiteral(":memory:")); QSignalSpy inserted(&cache, &AccountRepository::inserted), queued(&cache, &AccountRepository::folderQueued), changed(&cache, &AccountRepository::flagsUpdated);
        cache.insert(account); QTRY_COMPARE(inserted.size(), 1);
        MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); QSignalSpy result(&service, &MailService::synchronized);
        service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::All); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty());
        server.commands.clear(); cache.queueFolder(account.id, QStringLiteral("rename"), QStringLiteral("Archive"), QStringLiteral("Renamed"));
        cache.queueFolder(account.id, QStringLiteral("create"), {}, QStringLiteral("Parent")); cache.queueFolder(account.id, QStringLiteral("rename"), QStringLiteral("Parent"), QStringLiteral("Final"));
        QTRY_COMPARE(queued.size(), 3); for (const auto &item : queued) QVERIFY(item.at(1).toString().isEmpty());
        cache.queueFlag({account.id, QStringLiteral("Renamed"), 10, 7}, false, true); QTRY_COMPARE(changed.size(), 1); QVERIFY(changed.first().at(1).toString().isEmpty()); QVERIFY(server.commands.isEmpty());
        server.rejectFolderChanges = true; result.clear(); service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(!result.first().at(4).toString().isEmpty());
        qint64 through = 0; QList<PendingFolder> operations; QVERIFY(cache.folderRevision(account.id, through).isEmpty()); QVERIFY(cache.pendingFolders(account.id, 0, through, operations).isEmpty()); QCOMPARE(operations.size(), 3);
        server.rejectFolderChanges = false; result.clear(); service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString()));
        QVERIFY(server.extraFolders.contains("Renamed")); QVERIFY(server.extraFolders.contains("Final")); QVERIFY(!server.extraFolders.contains("Archive")); QVERIFY(server.folderFlags["Renamed"][7].contains("\\Seen")); QCOMPARE(server.fullFetchCount, 1);
        QVERIFY(cache.pendingFolders(account.id, 0, through, operations).isEmpty()); QVERIFY(operations.isEmpty());
        cache.queueFolder(account.id, QStringLiteral("delete"), QStringLiteral("Final"), {}); QTRY_COMPARE(queued.size(), 4); QVERIFY(server.extraFolders.contains("Final"));
        result.clear(); service.synchronize(account, QStringLiteral("dummy"), {}, &cache, MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty()); QVERIFY(!server.extraFolders.contains("Final"));
    }
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
        QSignalSpy progress(&service, &MailService::syncProgress);
        const auto sync = [&](MailService::SyncMode mode) { result.clear(); progress.clear(); service.synchronize(account, QStringLiteral("dummy"), QStringLiteral("INBOX"), &cache, mode); };
        sync(MailService::SyncMode::All); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(result.first().at(4).toString().isEmpty()); QCOMPARE(server.fullFetchCount, 4);
        for (const auto &event : progress) {
            const auto total = event.at(3).toLongLong();
            if (total >= 0) QCOMPARE(total, event.at(1).toString() == QStringLiteral("INBOX") ? qint64(3) : qint64(1));
        }
        server.messageFlags[1] = {"\\Seen"}; server.messageFlags[2] = {"\\Seen"};
        server.messages.insert(4, raw("Fri, 13 Nov 2026 08:59:59 +0000"));
        server.messages.insert(5, raw("Thu, 12 Nov 2026 23:30:00 -1000")); // UTC 09:30; SENTSINCE ignores timezone.
        server.folderMessages["Archive"].insert(11, raw("Thu, 05 Nov 2026 09:30:00 +0000"));
        server.extraFolders.insert("New"); server.folderMessages["New"].insert(70, raw("Sun, 01 Nov 2026 00:00:00 +0000"));
        sync(MailService::SyncMode::Recent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY2(result.first().at(4).toString().isEmpty(), qPrintable(result.first().at(4).toString())); QCOMPARE(server.fullFetchCount, 7);
        QMap<QString, QPair<qint64, qint64>> finalProgress;
        QSet<QString> unknown;
        for (const auto &event : progress) {
            const auto folder = event.at(1).toString();
            const auto done = event.at(2).toLongLong(), total = event.at(3).toLongLong();
            if (total < 0) { unknown.insert(folder); continue; }
            QVERIFY(unknown.contains(folder)); QVERIFY(done <= total);
            finalProgress[folder] = {done, total};
        }
        QCOMPARE(finalProgress.value(QStringLiteral("INBOX")), qMakePair(qint64(1), qint64(1)));
        QCOMPARE(finalProgress.value(QStringLiteral("Archive")), qMakePair(qint64(1), qint64(1)));
        QCOMPARE(finalProgress.value(QStringLiteral("New")), qMakePair(qint64(1), qint64(1)));
        sync(MailService::SyncMode::SelectedRecent); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY(result.first().at(4).toString().isEmpty());
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().at(1).toString(), QStringLiteral("INBOX"));
        QCOMPARE(progress.last().at(2).toLongLong(), qint64(0));
        QCOMPARE(progress.last().at(3).toLongLong(), qint64(0));
        QCOMPARE(server.fullFetchCount, 7);
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
