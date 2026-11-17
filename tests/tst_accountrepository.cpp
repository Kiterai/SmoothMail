#include "storage/accountrepository.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>
#include "mime/mimeparser.h"
#include "storage/downloadfile.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include "core/diagnostics.h"
#include <QScopeGuard>

using namespace smoothmail;
namespace {
Account makeAccount(const QString &email)
{
    return {QUuid::createUuid().toString(QUuid::WithoutBraces), email,
            {QStringLiteral("smtp.example.test"), 465, email},
            {QStringLiteral("imap.example.test"), 993, email}};
}
}

class AccountRepositoryTest final : public QObject
{
    Q_OBJECT
private slots:
    void addsUnreadIndexToExistingVersionFiveWithoutChangingMessages()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("existing.sqlite"));
        const auto account = makeAccount(QStringLiteral("existing@example.test"));
        const auto document = MimeParser::parse("Subject: existing\r\nDate: Wed, 18 Nov 2026 10:00:00 +0000\r\n\r\nkept");
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved);
            repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
            repository.saveSnapshot(account.id, QStringLiteral("INBOX"), {{QStringLiteral("INBOX"), u'/', {}, 1}},
                {{account.id, QStringLiteral("INBOX"), 10, 7, false, false, document.message}});
            QTRY_COMPARE(saved.size(), 1);
        }
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection); db.setDatabaseName(path); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec(QStringLiteral("DROP INDEX cache_messages_unread")));
        }
        QSqlDatabase::removeDatabase(connection);
        {
            AccountRepository repository(path); QSignalSpy loaded(&repository, &AccountRepository::messageLoaded);
            repository.loadMessage({account.id, QStringLiteral("INBOX"), 10, 7}); QTRY_COMPARE(loaded.size(), 1);
            QVERIFY(loaded.first().at(1).toString().isEmpty());
            QCOMPARE(qvariant_cast<MailRecord>(loaded.first().first()).document.rawSource, document.message.rawSource);
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection); db.setDatabaseName(path); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec(QStringLiteral("PRAGMA user_version"))); QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 5); query.finish();
            QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='cache_messages_unread'"))); QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 1); query.finish();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void incrementalStageKeepsUnchangedRowsAndPublishesOnlyDelta()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("delta.sqlite"));
        AccountRepository repository(path); const auto account = makeAccount(QStringLiteral("delta@example.test"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved),
            pages(&repository, &AccountRepository::pageLoaded), flags(&repository, &AccountRepository::flagsUpdated);
        repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        const QString folder = QStringLiteral("INBOX");
        const QList<MailFolder> folders{{folder, u'/', {}, 4}};
        const auto record = [&](qint64 uid, const QByteArray &date) {
            return MailRecord{account.id, folder, 10, uid, false, false,
                MimeParser::parse("Subject: delta\r\n" + date + "\r\n\r\nunchanged body").message};
        };
        const auto old = record(1, "Date: Wed, 18 Nov 2026 08:00:00 +0000"),
            recent = record(2, "Date: Wed, 18 Nov 2026 09:00:00 +0000"),
            removed = record(3, "Date: Wed, 18 Nov 2026 10:00:00 +0000"),
            undated = record(4, "");
        repository.saveSnapshot(account.id, folder, folders, {old, recent, removed, undated}); QTRY_COMPARE(saved.size(), 1);
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection); db.setDatabaseName(path); QVERIFY(db.open());
            QSqlQuery sql(db);
            QVERIFY(sql.exec(QStringLiteral("CREATE TABLE audit(kind TEXT)")));
            QVERIFY(sql.exec(QStringLiteral("CREATE TRIGGER audit_insert AFTER INSERT ON cache_messages BEGIN INSERT INTO audit VALUES('insert'); END")));
            QVERIFY(sql.exec(QStringLiteral("CREATE TRIGGER audit_delete AFTER DELETE ON cache_messages BEGIN INSERT INTO audit VALUES('delete'); END")));
            QVERIFY(sql.exec(QStringLiteral("CREATE TRIGGER audit_update AFTER UPDATE ON cache_messages BEGIN INSERT INTO audit VALUES('update'); END")));
            QVERIFY(sql.exec(QStringLiteral("CREATE TRIGGER audit_folder_insert AFTER INSERT ON cache_folders BEGIN INSERT INTO audit VALUES('folder-insert'); END")));
            QVERIFY(sql.exec(QStringLiteral("CREATE TRIGGER audit_folder_update AFTER UPDATE ON cache_folders BEGIN INSERT INTO audit VALUES('folder-update'); END")));
            QVERIFY(sql.exec(QStringLiteral("CREATE TRIGGER audit_folder_delete AFTER DELETE ON cache_folders BEGIN INSERT INTO audit VALUES('folder-delete'); END")));
            QVERIFY(sql.exec(QStringLiteral("CREATE TRIGGER audit_state_insert AFTER INSERT ON folder_state BEGIN INSERT INTO audit VALUES('state-insert'); END")));
            QVERIFY(sql.exec(QStringLiteral("CREATE TRIGGER audit_state_update AFTER UPDATE ON folder_state BEGIN INSERT INTO audit VALUES('state-update'); END")));
            const auto boundary = QDateTime::fromString(QStringLiteral("2026-11-18T09:00:00Z"), Qt::ISODate);
            QVERIFY(repository.beginSync(account.id, folder, 10, boundary).isEmpty());
            QSet<qint64> reused;
            // Old over-selected candidates must retain their original flags.
            QVERIFY(repository.reuseMessages({{1, true, true}, {2, true, false}, {5, false, false}}, reused, account.id).isEmpty());
            QCOMPARE(reused, (QSet<qint64>{1, 2}));
            QVERIFY(repository.stageMessages({record(5, "Date: Wed, 18 Nov 2026 10:30:00 +0000")}, account.id).isEmpty());
            repository.queueFlag(recent, true, true); QTRY_COMPARE(flags.size(), 1);
            QVERIFY(repository.commitSync(folders, -1, false, account.id).isEmpty()); repository.abortSync(account.id);
            MailQuery query; query.accountId = account.id; query.folder = folder;
            repository.loadPage(query); QTRY_COMPARE(pages.size(), 1);
            const auto page = qvariant_cast<MailPage>(pages.takeFirst().first()); QVERIFY(page.error.isEmpty()); QCOMPARE(page.total, 4);
            for (const auto &message : page.messages) {
                QVERIFY(message.uid != 3);
                if (message.uid == 1) { QVERIFY(!message.seen); QVERIFY(!message.important); }
                if (message.uid == 2) { QVERIFY(message.seen); QVERIFY(message.important); }
            }
            QVERIFY(sql.exec(QStringLiteral("SELECT COUNT(*) FROM audit WHERE kind='insert'"))); QVERIFY(sql.next()); QCOMPARE(sql.value(0).toInt(), 1); sql.finish();
            QVERIFY(sql.exec(QStringLiteral("SELECT COUNT(*) FROM audit WHERE kind='delete'"))); QVERIFY(sql.next()); QCOMPARE(sql.value(0).toInt(), 1); sql.finish();
            // A no-change full publication must not rewrite even one body row.
            QVERIFY(sql.exec(QStringLiteral("DELETE FROM pending_flags")));
            QVERIFY(sql.exec(QStringLiteral("DELETE FROM audit")));
            QVERIFY(repository.beginSync(account.id, folder, 10).isEmpty());
            QVERIFY(repository.reuseMessages({{1, false, false}, {2, true, true}, {4, false, false}, {5, false, false}}, reused, account.id).isEmpty());
            QCOMPARE(reused.size(), 4);
            auto stableFolders = folders; stableFolders[0].unread = 3;
            QVERIFY(repository.commitSync(stableFolders, 4, false, account.id).isEmpty()); repository.abortSync(account.id);
            QVERIFY(sql.exec(QStringLiteral("SELECT COUNT(*) FROM audit"))); QVERIFY(sql.next()); QCOMPARE(sql.value(0).toInt(), 0); sql.finish();
            // Invalid batches and a failed publication don't leak flags.
            QVERIFY(repository.beginSync(account.id, folder, 10, boundary).isEmpty());
            QVERIFY(!repository.reuseMessages({{2, false, false}, {2, true, true}}, reused, account.id).isEmpty());
            QVERIFY(reused.isEmpty());
            QVERIFY(!repository.reuseMessages(QList<AccountRepository::SyncFlags>(MailPageSize + 1, {1, false, false}), reused, account.id).isEmpty());
            QVERIFY(repository.reuseMessages({{2, false, false}}, reused, account.id).isEmpty());
            QVERIFY(!repository.commitSync({{QString{}, u'/', {}, 0}}, -1, false, account.id).isEmpty());
            repository.abortSync(account.id);
            repository.loadPage(query); QTRY_COMPARE(pages.size(), 1);
            for (const auto &message : qvariant_cast<MailPage>(pages.takeFirst().first()).messages)
                if (message.uid == 2) { QVERIFY(message.seen); QVERIFY(message.important); }
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void trackedReplySourceFollowsMovesWithoutRebindingCollidingUids()
    {
        AccountRepository repository(QStringLiteral(":memory:")); const auto account = makeAccount(QStringLiteral("reply@example.test"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved),
            moved(&repository, &AccountRepository::localOperationFinished), located(&repository, &AccountRepository::messageLocated),
            pages(&repository, &AccountRepository::pageLoaded), renamed(&repository, &AccountRepository::folderQueued), removed(&repository, &AccountRepository::removed);
        repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        const QList<MailFolder> folders{{QStringLiteral("A"), u'/', {}, 1}, {QStringLiteral("B"), u'/', {}, 1}, {QStringLiteral("Target"), u'/', {}, 0}};
        const auto parsed = MimeParser::parse("Subject: same subject\r\n\r\nbody");
        const MailRecord a{account.id, QStringLiteral("A"), 10, 7, false, false, parsed.message}, b{account.id, QStringLiteral("B"), 10, 7, false, false, parsed.message};
        repository.saveSnapshot(account.id, a.folder, folders, {a}); QTRY_COMPARE(saved.size(), 1);
        repository.saveSnapshot(account.id, b.folder, folders, {b}); QTRY_COMPARE(saved.size(), 2);
        repository.trackMessage(QStringLiteral("reply-a"), a); repository.trackMessage(QStringLiteral("reply-b"), b);
        repository.setMoveIntent(QStringLiteral("move-a"), a, QStringLiteral("Target")); repository.setMoveIntent(QStringLiteral("move-b"), b, QStringLiteral("Target"));
        QTRY_COMPARE(moved.size(), 2);
        repository.locateMessage(QStringLiteral("reply-b")); QTRY_COMPARE(located.size(), 1); QVERIFY(located.last().at(2).toString().isEmpty());
        auto identity = qvariant_cast<MailRecord>(located.takeFirst().at(1));
        QCOMPARE(identity.folder, QStringLiteral("Target")); QCOMPARE(identity.storageFolder, b.folder);
        MailQuery query; query.accountId = account.id; query.folder = identity.folder; query.pageSize = 1;
        query.revealUid = identity.uid; query.revealValidity = identity.uidValidity; query.revealStorageFolder = identity.storageFolder;
        repository.loadPage(query); QTRY_COMPARE(pages.size(), 1); const auto page = qvariant_cast<MailPage>(pages.takeFirst().first());
        QVERIFY(page.error.isEmpty()); QCOMPARE(page.total, 2); QCOMPARE(page.messages.first().storageFolder, b.folder);
        qint64 through = 0; QList<PendingMove> pending; QVERIFY(repository.moveRevision(account.id, through).isEmpty());
        QVERIFY(repository.pendingMoves(account.id, 0, through, pending).isEmpty());
        QVERIFY(repository.markMove(pending.first().revision).isEmpty()); QVERIFY(repository.finishMove(pending.first(), 20, 90).isEmpty());
        repository.queueFolder(account.id, QStringLiteral("rename"), QStringLiteral("Target"), QStringLiteral("Renamed")); QTRY_COMPARE(renamed.size(), 1);
        repository.locateMessage(QStringLiteral("reply-a")); QTRY_COMPARE(located.size(), 1); QVERIFY(located.last().at(2).toString().isEmpty());
        identity = qvariant_cast<MailRecord>(located.takeFirst().at(1));
        QCOMPARE(identity.folder, QStringLiteral("Renamed")); QCOMPARE(identity.storageFolder, QStringLiteral("Target")); QCOMPARE(identity.uid, 90); QCOMPARE(identity.uidValidity, 20);
        // An old handle must not bind to UID 90 from a new UIDVALIDITY.
        auto replacement = identity; replacement.folder = QStringLiteral("Target"); replacement.uidValidity = 21; replacement.document = parsed.message;
        repository.saveSnapshot(account.id, replacement.folder, folders, {replacement}); QTRY_COMPARE(saved.size(), 3);
        repository.locateMessage(QStringLiteral("reply-a")); QTRY_COMPARE(located.size(), 1); QVERIFY(!located.takeFirst().at(2).toString().isEmpty());
        repository.forgetMessage(QStringLiteral("reply-a")); repository.locateMessage(QStringLiteral("reply-a"));
        QTRY_COMPARE(located.size(), 1); QVERIFY(!located.takeFirst().at(2).toString().isEmpty());
        repository.remove(account.id); QTRY_COMPARE(removed.size(), 1); repository.locateMessage(QStringLiteral("reply-b"));
        QTRY_COMPARE(located.size(), 1); QVERIFY(!located.takeFirst().at(2).toString().isEmpty());
    }
    void interleavedAccountStagesCommitAndAbortIndependently()
    {
        AccountRepository repository(QStringLiteral(":memory:"));
        const auto a = makeAccount(QStringLiteral("a@example.test")), b = makeAccount(QStringLiteral("b@example.test"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted), pages(&repository, &AccountRepository::pageLoaded);
        repository.insert(a); repository.insert(b); QTRY_COMPARE(inserted.size(), 2);
        const auto folder = QStringLiteral("INBOX"); const QList<MailFolder> folders{{folder, u'/', {}, 1}};
        const MailRecord first{a.id, folder, 10, 1, false, false, MimeParser::parse("Subject: account A\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n\r\nA").message};
        const MailRecord second{b.id, folder, 20, 1, false, false, MimeParser::parse("Subject: account B\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n\r\nB").message};
        QVERIFY(repository.beginSync(a.id, folder, 10).isEmpty());
        QVERIFY(repository.beginSync(b.id, folder, 20).isEmpty());
        QVERIFY(repository.stageMessages({first}, a.id).isEmpty());
        QVERIFY(repository.stageMessages({second}, b.id).isEmpty());
        QVERIFY(!repository.stageMessages({first}, b.id).isEmpty());
        QVERIFY(repository.commitSync(folders, 1, false, b.id).isEmpty()); repository.abortSync(b.id);
        QVERIFY(repository.commitSync(folders, 1, false, a.id).isEmpty()); repository.abortSync(a.id);
        // Account A's recent-window pruning and abort must not touch B's UID 1.
        QVERIFY(repository.beginSync(b.id, folder, 20).isEmpty());
        QVERIFY(repository.stageMessages({second}, b.id).isEmpty());
        QVERIFY(repository.beginSync(a.id, folder, 10, QDateTime::fromString(QStringLiteral("2026-11-13T09:00:00Z"), Qt::ISODate)).isEmpty());
        repository.abortSync(a.id);
        QVERIFY(repository.commitSync(folders, 1, false, b.id).isEmpty()); repository.abortSync(b.id);
        for (const auto &record : {first, second}) {
            MailQuery query; query.accountId = record.accountId; query.folder = folder; repository.loadPage(query);
            QTRY_COMPARE(pages.size(), 1); const auto page = qvariant_cast<MailPage>(pages.takeFirst().first());
            QCOMPARE(page.total, 1); QCOMPARE(page.messages.first().document.subject, record.document.subject);
            QCOMPARE(page.messages.first().uidValidity, record.uidValidity);
        }
        QVERIFY(!repository.commitSync(folders, 1, false, a.id).isEmpty());
    }
    void folderScopedCommitPreservesOtherFoldersAndPendingFlags()
    {
        AccountRepository repository(QStringLiteral(":memory:")); const auto account = makeAccount(QStringLiteral("scoped@example.test"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved), changed(&repository, &AccountRepository::flagsUpdated), cached(&repository, &AccountRepository::cacheLoaded), pages(&repository, &AccountRepository::pageLoaded);
        repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        const auto parsed = MimeParser::parse("Subject: retained\r\n\r\nbody"); const QList<MailFolder> folders{{QStringLiteral("INBOX"), u'/', {}, 1}, {QStringLiteral("Other"), u'/', {}, 1}};
        const MailRecord other{account.id, QStringLiteral("Other"), 10, 7, false, false, parsed.message};
        repository.saveSnapshot(account.id, other.folder, folders, {other}); QTRY_COMPARE(saved.size(), 1);
        repository.queueFlag(other, true, true); QTRY_COMPARE(changed.size(), 1);
        QVERIFY(repository.beginSync(account.id, QStringLiteral("INBOX"), 10).isEmpty()); QVERIFY(repository.commitSync({folders.first()}, 0, true).isEmpty()); repository.abortSync();
        MailQuery query; query.accountId = account.id; query.folder = other.folder; repository.loadPage(query); QTRY_COMPARE(pages.size(), 1);
        QCOMPARE(qvariant_cast<MailPage>(pages.first().first()).total, 1); QVERIFY(qvariant_cast<MailPage>(pages.first().first()).messages.first().important);
        qint64 revision = 0; QList<PendingFlag> flags; QVERIFY(repository.flagRevision(revision).isEmpty()); QVERIFY(repository.pendingFlags(account.id, other.folder, 0, revision, flags).isEmpty()); QCOMPARE(flags.size(), 1);
        repository.loadCache(account.id, true); QTRY_COMPARE(cached.size(), 1); bool found = false;
        for (const auto &folder : qvariant_cast<QList<MailFolder>>(cached.first().at(2))) if (folder.name == other.folder) { found = true; QCOMPARE(folder.unread, 1); }
        QVERIFY(found);
    }
    void metadataOnlySyncWithoutSelectableMailboxDoesNotBindSqlNull()
    {
        AccountRepository repository(QStringLiteral(":memory:")); const auto account = makeAccount(QStringLiteral("noselect@example.test"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted), cached(&repository, &AccountRepository::cacheLoaded), initial(&repository, &AccountRepository::initialFetchRequired);
        repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        QVERIFY(repository.beginSync(account.id, {}, 0).isEmpty());
        QVERIFY(repository.commitSync({{QStringLiteral("Root"), u'/', {"\\Noselect"}, 0}}, 0).isEmpty()); repository.abortSync();
        repository.ensureFolderLoaded(account.id, {});
        repository.loadCache(account.id, true); QTRY_COMPARE(cached.size(), 1); QVERIFY(initial.isEmpty());
        const auto folders = qvariant_cast<QList<MailFolder>>(cached.first().at(2)); QCOMPARE(folders.size(), 1); QCOMPARE(folders.first().name, QStringLiteral("Root"));
    }
    void folderMetadataErrorsAreDiagnosableWithoutPrivateNames_data()
    {
        QTest::addColumn<int>("reason"); QTest::addColumn<bool>("streaming");
        for (int reason = 0; reason < 4; ++reason) for (const bool streaming : {false, true}) QTest::newRow(qPrintable(QStringLiteral("reason-%1-%2").arg(reason).arg(streaming ? QStringLiteral("sync") : QStringLiteral("snapshot")))) << reason << streaming;
    }
    void folderMetadataErrorsAreDiagnosableWithoutPrivateNames()
    {
        QFETCH(int, reason); QFETCH(bool, streaming);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(diagnostics::start(directory.path())); const auto finishSession = qScopeGuard([] { diagnostics::finish(); });
        AccountRepository repository(QStringLiteral(":memory:")); const auto account = makeAccount(QStringLiteral("private-account@example.test"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved), failed(&repository, &AccountRepository::failed), cached(&repository, &AccountRepository::cacheLoaded);
        repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        const QList<MailFolder> original{{QStringLiteral("INBOX"), u'/', {}, 0}};
        repository.saveSnapshot(account.id, QStringLiteral("INBOX"), original, {}); QTRY_COMPARE(saved.size(), 1);
        const MailFolder named{QStringLiteral("private-folder-sentinel"), u'/', {}, 0};
        QList<MailFolder> invalid;
        if (reason == 0) invalid = {{QString{}, u'/', {"\\Noselect"}, 0}};
        if (reason == 1) invalid = {named, named, named};
        if (reason == 2) { auto negative = named; negative.unread = -7; invalid = {negative}; }
        if (reason == 3) invalid = {{QString{}, u'/', {"\\Noselect"}, -7}, named, named};
        diagnostics::OperationId operation = 0;
        if (streaming) {
            operation = diagnostics::nextOperation();
            QVERIFY(repository.beginSync(account.id, QStringLiteral("INBOX"), 10, {}, operation).isEmpty());
            QCOMPARE(repository.commitSync(invalid, 0), QStringLiteral("フォルダ情報が不正です。")); repository.abortSync();
        } else {
            repository.saveSnapshot(account.id, QStringLiteral("INBOX"), invalid, {}); QTRY_COMPARE(failed.size(), 1);
            QCOMPARE(failed.first().at(1).toString(), QStringLiteral("フォルダ情報が不正です。")); QCOMPARE(saved.size(), 1);
        }
        repository.loadCache(account.id, true); QTRY_COMPARE(cached.size(), 1);
        const auto folders = qvariant_cast<QList<MailFolder>>(cached.first().at(2)); QCOMPARE(folders.size(), 1); QCOMPARE(folders.first().name, QStringLiteral("INBOX"));
        QByteArray log;
        for (const auto &name : QDir(directory.path()).entryList({QStringLiteral("*.log")}, QDir::Files)) { QFile file(directory.filePath(name)); QVERIFY(file.open(QIODevice::ReadOnly)); log += file.readAll(); }
        const QByteArray source = streaming ? "sync-commit" : "snapshot";
        const auto index = log.indexOf("folder-metadata-invalid source=" + source); QVERIFY(index >= 0);
        const auto line = log.mid(index).split('\n').first();
        QVERIFY(line.contains("total=" + QByteArray::number(invalid.size())));
        QVERIFY(line.contains(QByteArray("empty_count=") + (reason == 0 || reason == 3 ? "1" : "0")));
        QVERIFY(line.contains(QByteArray("duplicate_count=") + (reason == 1 ? "2" : reason == 3 ? "1" : "0")));
        QVERIFY(line.contains(QByteArray("negative_unread_count=") + (reason >= 2 ? "1" : "0")));
        if (reason == 0 || reason == 3) QVERIFY(line.contains("first_empty_index=1 first_empty_noselect=1"));
        if (reason == 1) QVERIFY(line.contains("first_duplicate_index=2 duplicate_of_index=1"));
        if (reason == 3) QVERIFY(line.contains("first_duplicate_index=3 duplicate_of_index=2"));
        if (reason >= 2) QVERIFY(line.contains("first_negative_unread=-7"));
        if (streaming) {
            const auto suffix = " op=" + QByteArray::number(operation);
            QVERIFY(line.endsWith(suffix)); QVERIFY(log.contains("sync-cache-rollback" + suffix)); QVERIFY(log.contains("sync-cache-failed" + suffix));
        } else { QVERIFY(log.contains("cache-save-rollback op=")); QVERIFY(log.contains("cache-save-failed op=")); }
        for (const auto &secret : {account.id.toUtf8(), account.email.toUtf8(), named.name.toUtf8()}) QVERIFY(!log.contains(secret));
    }
    void movesKeepCollidingMessagesDistinctAndUndoAcrossAcknowledgement()
    {
        AccountRepository repository(QStringLiteral(":memory:")); const auto account = makeAccount(QStringLiteral("moves@example.test"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved), changed(&repository, &AccountRepository::localOperationFinished), pages(&repository, &AccountRepository::pageLoaded), bodies(&repository, &AccountRepository::messageLoaded), flags(&repository, &AccountRepository::flagsUpdated);
        repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        const QList<MailFolder> folders{{QStringLiteral("A"), u'/', {}, 1}, {QStringLiteral("B"), u'/', {}, 1}, {QStringLiteral("Target"), u'/', {}, 0}};
        const auto a = MimeParser::parse("Subject: A\r\n\r\nbody A"), b = MimeParser::parse("Subject: B\r\n\r\nbody B");
        const MailRecord first{account.id, QStringLiteral("A"), 10, 7, false, false, a.message}, second{account.id, QStringLiteral("B"), 10, 7, false, false, b.message};
        repository.saveSnapshot(account.id, first.folder, folders, {first}); QTRY_COMPARE(saved.size(), 1);
        repository.saveSnapshot(account.id, second.folder, folders, {second}); QTRY_COMPARE(saved.size(), 2);
        repository.setFlagIntent(QStringLiteral("flag"), first, true, true); QTRY_COMPARE(flags.size(), 1);
        repository.setMoveIntent(QStringLiteral("a"), first, QStringLiteral("Target")); repository.setMoveIntent(QStringLiteral("b"), second, QStringLiteral("Target")); QTRY_COMPARE(changed.size(), 2);
        for (const auto &result : changed) QVERIFY2(result.at(1).toString().isEmpty(), qPrintable(result.at(1).toString()));
        MailQuery query; query.accountId = account.id; query.folder = QStringLiteral("Target"); query.pageSize = 1;
        repository.loadPage(query); QTRY_COMPARE(pages.size(), 1); auto page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, 2); QCOMPARE(page.messages.size(), 1);
        const auto record = page.messages.first(); QVERIFY(!record.storageFolder.isEmpty()); repository.loadMessage(record); QTRY_COMPARE(bodies.size(), 1);
        QCOMPARE(qvariant_cast<MailRecord>(bodies.first().first()).document.subject, record.storageFolder);
        qint64 through = 0; QList<PendingMove> pending; QVERIFY(repository.moveRevision(account.id, through).isEmpty()); QVERIFY(repository.pendingMoves(account.id, 0, through, pending).isEmpty()); QCOMPARE(pending.size(), 2);
        const auto moving = pending.first(); QVERIFY(repository.markMove(moving.revision).isEmpty());
        // Two undo/redo changes while MOVE is in flight must retain the reverse intent.
        repository.setMoveIntent(QStringLiteral("a"), first, first.folder); repository.setMoveIntent(QStringLiteral("a"), first, QStringLiteral("Target")); repository.setMoveIntent(QStringLiteral("a"), first, first.folder); QTRY_COMPARE(changed.size(), 5);
        QVERIFY(repository.finishMove(moving, 20, 90).isEmpty());
        QVERIFY(repository.moveRevision(account.id, through).isEmpty()); QVERIFY(repository.pendingMoves(account.id, 0, through, pending).isEmpty()); QCOMPARE(pending.size(), 2);
        const auto reverse = pending.last(); QCOMPARE(reverse.identity.folder, QStringLiteral("Target")); QCOMPARE(reverse.identity.uid, 90); QCOMPARE(reverse.destination, first.folder);
        repository.setFlagIntent(QStringLiteral("flag"), first, true, false); QTRY_COMPARE(flags.size(), 2); QVERIFY(flags.last().at(1).toString().isEmpty());
        qint64 flagThrough = 0; QList<PendingFlag> edits; QVERIFY(repository.flagRevision(flagThrough).isEmpty()); QVERIFY(repository.pendingFlags(account.id, QStringLiteral("Target"), 0, flagThrough, edits).isEmpty()); QCOMPARE(edits.size(), 1); QCOMPARE(edits.first().identity.uid, 90); QVERIFY(!edits.first().enabled);
        QVERIFY(repository.markMove(reverse.revision).isEmpty()); QVERIFY(repository.finishMove(reverse, 10, 8).isEmpty());
        repository.setMoveIntent(QStringLiteral("a"), first, QStringLiteral("Target")); QTRY_COMPARE(changed.size(), 6);
        QVERIFY(repository.moveRevision(account.id, through).isEmpty()); QVERIFY(repository.pendingMoves(account.id, 0, through, pending).isEmpty()); QCOMPARE(pending.last().identity.uid, 8);
        repository.setMoveIntent(QStringLiteral("a"), first, first.folder); QTRY_COMPARE(changed.size(), 7);
        QVERIFY(!repository.markMove(pending.last().revision).isEmpty()); // A stale batch cannot send a cancelled MOVE.
        repository.setMoveIntent(QStringLiteral("invalid"), second, QStringLiteral("Missing")); QTRY_COMPARE(changed.size(), 8); QVERIFY(!changed.last().at(1).toString().isEmpty());
    }
    void fetchedEmptyFoldersDoNotTriggerReloadAndCreationCanBeUndone()
    {
        AccountRepository repository(QStringLiteral(":memory:")); const auto account = makeAccount(QStringLiteral("initial@example.test"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved), fetch(&repository, &AccountRepository::initialFetchRequired), queued(&repository, &AccountRepository::folderQueued), changed(&repository, &AccountRepository::localOperationFinished);
        repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        const QList<MailFolder> folders{{QStringLiteral("INBOX"), u'/', {}, 0}, {QStringLiteral("Other"), u'/', {}, 0}};
        repository.saveSnapshot(account.id, QStringLiteral("INBOX"), folders, {}); QTRY_COMPARE(saved.size(), 1);
        repository.ensureFolderLoaded(account.id, QStringLiteral("INBOX")); repository.ensureFolderLoaded(account.id, QStringLiteral("Other")); QTRY_COMPARE(fetch.size(), 1); QCOMPARE(fetch.first().at(1).toString(), QStringLiteral("Other"));
        repository.restoreCreatedFolder(account.id, QStringLiteral("New")); QTRY_COMPARE(queued.size(), 1);
        repository.queueFolder(account.id, QStringLiteral("rename"), QStringLiteral("New"), QStringLiteral("Renamed")); QTRY_COMPARE(queued.size(), 2);
        repository.cancelCreatedFolder(account.id, QStringLiteral("Renamed")); QTRY_COMPARE(queued.size(), 3); QVERIFY(queued.last().at(1).toString().isEmpty());
        qint64 through = 0; QList<PendingFolder> pending; QVERIFY(repository.folderRevision(account.id, through).isEmpty()); QVERIFY(repository.pendingFolders(account.id, 0, through, pending).isEmpty()); QCOMPARE(pending.size(), 3); QCOMPARE(pending.last().operation, QStringLiteral("delete-empty"));
        repository.restoreCreatedFolder(account.id, QStringLiteral("Renamed")); QTRY_COMPARE(queued.size(), 4);
        pending.clear(); QVERIFY(repository.folderRevision(account.id, through).isEmpty()); QVERIFY(repository.pendingFolders(account.id, 0, through, pending).isEmpty()); QCOMPARE(pending.size(), 4); QCOMPARE(pending.last().destination, QStringLiteral("Renamed"));
    }
    void deletionCannotQueueReloadAfterStateDestruction()
    {
        for (int i = 0; i < 20; ++i) {
            auto *repository = new AccountRepository(QStringLiteral(":memory:"));
            const auto account = makeAccount(QStringLiteral("shutdown@example.test"));
            QSignalSpy inserted(repository, &AccountRepository::inserted);
            repository->insert(account); QTRY_COMPARE(inserted.size(), 1);
            repository->remove(account.id);
            delete repository; // Race the worker's self-queued reload with its deletion barrier.
        }
    }
    void folderJournalKeepsMailReadableAcrossRestartAndRename()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath(QStringLiteral("folders.sqlite"));
        const auto account = makeAccount(QStringLiteral("folders@example.test"));
        const auto parsed = MimeParser::parse("Subject: retained\r\n\r\nreadable while pending"); QVERIFY(parsed.ok());
        const QList<MailFolder> folders{{QStringLiteral("INBOX"), u'/', {}, 0}, {QStringLiteral("Archive"), u'/', {}, 1}, {QStringLiteral("Archive/Child"), u'/', {}, 0}};
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved), queued(&repository, &AccountRepository::folderQueued);
            repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
            repository.saveSnapshot(account.id, QStringLiteral("Archive"), folders, {{account.id, QStringLiteral("Archive"), 10, 7, false, false, parsed.message}}); QTRY_COMPARE(saved.size(), 1);
            repository.queueFolder(account.id, QStringLiteral("rename"), QStringLiteral("Archive"), QStringLiteral("Renamed"));
            repository.queueFolder(account.id, QStringLiteral("create"), {}, QStringLiteral("Renamed/New"));
            repository.queueFolder(account.id, QStringLiteral("rename"), QStringLiteral("Renamed/New"), QStringLiteral("Renamed/New2"));
            repository.queueFolder(account.id, QStringLiteral("delete"), QStringLiteral("Renamed/Child"), {});
            QTRY_COMPARE(queued.size(), 4); for (const auto &item : queued) QVERIFY2(item.at(1).toString().isEmpty(), qPrintable(item.at(1).toString()));
        }
        AccountRepository repository(path); QSignalSpy cached(&repository, &AccountRepository::cacheLoaded), pages(&repository, &AccountRepository::pageLoaded), bodies(&repository, &AccountRepository::messageLoaded), flags(&repository, &AccountRepository::flagsUpdated);
        repository.loadCache(account.id, true); QTRY_COMPARE(cached.size(), 1);
        QStringList names; for (const auto &folder : qvariant_cast<QList<MailFolder>>(cached.first().at(2))) names.append(folder.name);
        QVERIFY(names.contains(QStringLiteral("Renamed/New2"))); QVERIFY(!names.contains(QStringLiteral("Archive"))); QVERIFY(!names.contains(QStringLiteral("Renamed/Child")));
        MailQuery query; query.accountId = account.id; query.folder = QStringLiteral("Renamed"); repository.loadPage(query); QTRY_COMPARE(pages.size(), 1);
        auto page = qvariant_cast<MailPage>(pages.last().first()); QCOMPARE(page.total, 1); QCOMPARE(page.messages.first().folder, query.folder);
        repository.loadMessage(page.messages.first()); QTRY_COMPARE(bodies.size(), 1); QVERIFY(qvariant_cast<MailRecord>(bodies.first().first()).document.parts.first().text.contains(QStringLiteral("readable")));
        repository.queueFlag(page.messages.first(), true, true); QTRY_COMPARE(flags.size(), 1); QVERIFY(flags.first().at(1).toString().isEmpty());
        qint64 through = 0; QList<PendingFolder> operations; QVERIFY(repository.folderRevision(account.id, through).isEmpty()); QVERIFY(repository.pendingFolders(account.id, 0, through, operations).isEmpty()); QCOMPARE(operations.size(), 4);
        const QList<MailFolder> renamed{{QStringLiteral("INBOX"), u'/', {}, 0}, {QStringLiteral("Renamed"), u'/', {}, 1}, {QStringLiteral("Renamed/Child"), u'/', {}, 0}};
        QVERIFY(repository.finishFolder(operations.first(), true, renamed).isEmpty());
        qint64 flagRevision = 0; QList<PendingFlag> pending; QVERIFY(repository.flagRevision(flagRevision).isEmpty()); QVERIFY(repository.pendingFlags(account.id, query.folder, 0, flagRevision, pending).isEmpty()); QCOMPARE(pending.size(), 1);
        pages.clear(); repository.loadPage(query); QTRY_COMPARE(pages.size(), 1); page = qvariant_cast<MailPage>(pages.first().first()); QCOMPARE(page.total, 1); QVERIFY(page.messages.first().important);
    }
    void accountEditsDoNotInvalidateActiveSnapshot()
    {
        AccountRepository repository(QStringLiteral(":memory:")); auto account = makeAccount(QStringLiteral("edit-active@example.test")); const auto original = account;
        QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved), pages(&repository, &AccountRepository::pageLoaded), flags(&repository, &AccountRepository::flagsUpdated), operations(&repository, &AccountRepository::localOperationFinished);
        repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        const auto parsed = MimeParser::parse("Subject: old server\r\n\r\nbody"); const MailRecord message{account.id, QStringLiteral("INBOX"), 10, 1, false, false, parsed.message};
        const QList<MailFolder> folders{{message.folder, u'/', {}, 1}}; repository.saveSnapshot(account.id, message.folder, folders, {message}); QTRY_COMPARE(saved.size(), 1);
        repository.setFlagIntent(QStringLiteral("old-server-flag"), message, true, true); QTRY_COMPARE(flags.size(), 1);
        account.imap.host = QStringLiteral("new.example.test"); repository.update(account); QTRY_COMPARE(inserted.size(), 2);
        QVERIFY(repository.prepareSync(original).isEmpty());
        MailQuery query; query.accountId = account.id; query.folder = message.folder; repository.loadPage(query); QTRY_COMPARE(pages.size(), 1); QCOMPARE(qvariant_cast<MailPage>(pages.takeFirst().first()).total, 1);
        QVERIFY(repository.beginSync(account.id, message.folder, 10).isEmpty()); QVERIFY(repository.stageMessages({message}).isEmpty()); QVERIFY(repository.commitSync(folders, 1).isEmpty()); repository.abortSync();
        QVERIFY(repository.prepareSync(account).isEmpty()); repository.loadPage(query); QTRY_COMPARE(pages.size(), 1); QCOMPARE(qvariant_cast<MailPage>(pages.first().first()).total, 0);
        // A new server may legitimately reuse both UIDVALIDITY and UID. Never
        // recreate a missing handle and apply an old Undo to its unrelated mail.
        repository.saveSnapshot(account.id, message.folder, folders, {message}); QTRY_COMPARE(saved.size(), 2);
        repository.setFlagIntent(QStringLiteral("old-server-flag"), message, true, false); QTRY_COMPARE(operations.size(), 1); QVERIFY(!operations.first().at(1).toString().isEmpty());
    }
    void pendingFlagsSurviveRestartAndPreserveNewerEdits()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath(QStringLiteral("flags.sqlite"));
        const auto account = makeAccount(QStringLiteral("outbox@example.test"));
        const auto parsed = MimeParser::parse("Subject: outbox\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n\r\nbody"); QVERIFY(parsed.ok());
        const MailRecord identity{account.id, QStringLiteral("INBOX"), 10, 42, false, false, parsed.message};
        const QList<MailFolder> folders{{identity.folder, u'/', {}, 1}};
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved), changed(&repository, &AccountRepository::flagsUpdated);
            repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
            repository.saveSnapshot(account.id, identity.folder, folders, {identity}); QTRY_COMPARE(saved.size(), 1);
            repository.queueFlag(identity, false, true); repository.queueFlag(identity, true, true); QTRY_COMPARE(changed.size(), 2);
            for (const auto &result : changed) QVERIFY(result.at(1).toString().isEmpty());
        }
        {
            AccountRepository repository(path); qint64 through = 0; QList<PendingFlag> edits;
            QVERIFY(repository.flagRevision(through).isEmpty()); QVERIFY(repository.pendingFlags(account.id, identity.folder, 0, through, edits).isEmpty()); QCOMPARE(edits.size(), 2);
            QSignalSpy changed(&repository, &AccountRepository::flagsUpdated), pages(&repository, &AccountRepository::pageLoaded);
            repository.queueFlag(identity, false, false); QTRY_COMPARE(changed.size(), 1);
            QVERIFY(repository.acknowledgeFlag(edits.first(), true, false).isEmpty()); // Old ack must not erase the newer unread edit.
            QVERIFY(repository.acknowledgeFlag(edits.last(), false, false).isEmpty()); // Server rejects/overrides important flag.
            QVERIFY(repository.beginSync(account.id, identity.folder, 10).isEmpty());
            auto server = identity; server.seen = true;
            QVERIFY(repository.stageMessages({server}).isEmpty()); QVERIFY(repository.commitSync(folders, 1).isEmpty()); repository.abortSync();
            MailQuery query; query.accountId = account.id; query.folder = identity.folder;
            repository.loadPage(query); QTRY_COMPARE(pages.size(), 1);
            const auto page = qvariant_cast<MailPage>(pages.last().first()); QCOMPARE(page.total, 1);
            QVERIFY(!page.messages.first().seen); QVERIFY(!page.messages.first().important);
            QVERIFY(repository.flagRevision(through).isEmpty()); QVERIFY(repository.pendingFlags(account.id, identity.folder, 0, through, edits).isEmpty()); QCOMPARE(edits.size(), 1); QVERIFY(!edits.first().enabled);
            QDateTime boundary; QVERIFY(repository.recentBoundary(account.id, identity.folder, 10, boundary).isEmpty());
            QCOMPARE(boundary, parsed.message.date.addSecs(-3600));
            QVERIFY(repository.recentBoundary(account.id, identity.folder, 20, boundary).isEmpty()); QVERIFY(!boundary.isValid());
            QVERIFY(repository.beginSync(account.id, identity.folder, 20).isEmpty());
            server.uidValidity = 20; QVERIFY(repository.stageMessages({server}).isEmpty()); QVERIFY(repository.commitSync(folders, 1).isEmpty()); repository.abortSync();
            QVERIFY(repository.pendingFlags(account.id, identity.folder, 0, through, edits).isEmpty()); QVERIFY(edits.isEmpty());
        }
    }
    void upgradesV1CacheWithoutLoadingAllBodies()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath(QStringLiteral("v1.sqlite"));
        const auto account = makeAccount(QStringLiteral("upgrade@example.test"));
        const auto parsed = MimeParser::parse("From: fixture@example.test\r\nSubject: legacy\r\n\r\nlegacy searchable body"); QVERIFY(parsed.ok());
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved);
            repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
            repository.saveSnapshot(account.id, QStringLiteral("INBOX"), {{QStringLiteral("INBOX"), u'/', {}, 1}}, {{account.id, QStringLiteral("INBOX"), 7, 42, false, true, parsed.message}});
            QTRY_COMPARE(saved.size(), 1);
        }
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection); db.setDatabaseName(path); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec(QStringLiteral("DROP TABLE cache_index"))); QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=1"))); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        {
            AccountRepository repository(path); QSignalSpy cached(&repository, &AccountRepository::cacheLoaded), pages(&repository, &AccountRepository::pageLoaded), bodies(&repository, &AccountRepository::messageLoaded);
            repository.loadCache(account.id, true); QTRY_COMPARE(cached.size(), 1); QVERIFY(qvariant_cast<QList<MailRecord>>(cached.first().at(3)).isEmpty());
            MailQuery query; query.accountId = account.id; query.folder = QStringLiteral("INBOX"); query.text = QStringLiteral("SEARCHABLE");
            repository.loadPage(query); QTRY_COMPARE(pages.size(), 1);
            const auto page = qvariant_cast<MailPage>(pages.first().first()); QVERIFY2(page.error.isEmpty(), qPrintable(page.error)); QCOMPARE(page.total, 1);
            QCOMPARE(page.messages.first().uid, 42); QVERIFY(page.messages.first().document.rawSource.isEmpty());
            repository.loadMessage(page.messages.first()); QTRY_COMPARE(bodies.size(), 1);
            QCOMPARE(qvariant_cast<MailRecord>(bodies.first().first()).document.rawSource, parsed.message.rawSource);
        }
    }
    void pagesSearchAndStreamingRollback()
    {
        AccountRepository repository(QStringLiteral(":memory:"));
        const auto account = makeAccount(QStringLiteral("pages@example.test"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted);
        QSignalSpy saved(&repository, &AccountRepository::snapshotSaved);
        QSignalSpy pages(&repository, &AccountRepository::pageLoaded);
        QSignalSpy bodies(&repository, &AccountRepository::messageLoaded);
        repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        QList<MailRecord> records;
        for (int i = 1; i <= 234; ++i) {
            const auto raw = "From: Sender <sender@example.test>\r\nSubject: " + QByteArray::number(i).rightJustified(3, '0')
                + "\r\nMessage-ID: <" + QByteArray::number(i) + "@example.test>\r\n\r\n" + (i == 234 ? "needle body" : "ordinary body");
            const auto parsed = MimeParser::parse(raw); QVERIFY(parsed.ok());
            records.append({account.id, QStringLiteral("INBOX"), 10, i, bool(i % 3), !(i % 2), parsed.message});
        }
        const QList<MailFolder> folders{{QStringLiteral("INBOX"), u'/', {}, 78}};
        repository.saveSnapshot(account.id, QStringLiteral("INBOX"), folders, records); QTRY_COMPARE(saved.size(), 1);
        MailQuery query; query.accountId = account.id; query.folder = QStringLiteral("INBOX"); query.sortColumn = 3; query.order = Qt::AscendingOrder; query.page = 2;
        repository.loadPage(query); QTRY_COMPARE(pages.size(), 1);
        auto page = qvariant_cast<MailPage>(pages.takeFirst().first());
        QCOMPARE(page.total, 234); QCOMPARE(page.messages.size(), 100); QCOMPARE(page.messages.first().uid, 101);
        QVERIFY(page.messages.first().document.rawSource.isEmpty()); QVERIFY(page.messages.first().document.parts.isEmpty());
        query.page = 999; repository.loadPage(query); QTRY_COMPARE(pages.size(), 1);
        page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.query.page, 3); QCOMPARE(page.messages.size(), 34);
        query.text = QStringLiteral("NEEDLE"); repository.loadPage(query); QTRY_COMPARE(pages.size(), 1);
        page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, 1); QCOMPARE(page.messages.first().uid, 234);
        repository.loadMessage(page.messages.first()); QTRY_COMPARE(bodies.size(), 1);
        QVERIFY(qvariant_cast<MailRecord>(bodies.first().first()).document.parts.first().text.contains(QStringLiteral("needle")));
        query.text.clear(); query.unreadOnly = true; query.importantOnly = true;
        repository.loadPage(query); QTRY_COMPARE(pages.size(), 1); page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, 39);
        query.unreadOnly = false; query.importantOnly = false; query.revealUid = 201; query.revealValidity = 10;
        repository.loadPage(query); QTRY_COMPARE(pages.size(), 1); page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.query.page, 3);
        QVERIFY(repository.beginSync(account.id, QStringLiteral("INBOX"), 20).isEmpty());
        auto changed = records.first(); changed.uidValidity = 20;
        QVERIFY(repository.stageMessages({changed}).isEmpty());
        QVERIFY(!repository.commitSync(folders, 2).isEmpty()); repository.abortSync();
        query.revealUid = 0; query.page = 1;
        repository.loadPage(query); QTRY_COMPARE(pages.size(), 1); page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, 234); QCOMPARE(page.messages.first().uidValidity, 10);
        QVERIFY(repository.beginSync(account.id, QStringLiteral("INBOX"), 20).isEmpty());
        QVERIFY(repository.stageMessages({changed}).isEmpty()); QVERIFY(repository.commitSync(folders, 1).isEmpty()); repository.abortSync();
        repository.loadPage(query); QTRY_COMPARE(pages.size(), 1); page = qvariant_cast<MailPage>(pages.takeFirst().first()); QCOMPARE(page.total, 1); QCOMPARE(page.messages.first().uidValidity, 20);
    }
    void updatesAndDeletesAccount()
    {
        AccountRepository repository(QStringLiteral(":memory:"));
        QSignalSpy loaded(&repository, &AccountRepository::loaded);
        QSignalSpy saved(&repository, &AccountRepository::inserted);
        QSignalSpy removed(&repository, &AccountRepository::removed);
        QSignalSpy failed(&repository, &AccountRepository::failed);
        QTRY_VERIFY(!loaded.isEmpty()); loaded.clear();
        auto account = makeAccount(QStringLiteral("edit@example.test"));
        repository.insert(account); QTRY_COMPARE(saved.size(), 1); QTRY_VERIFY(!loaded.isEmpty()); loaded.clear();
        account.imap.host = QStringLiteral("new.example.test");
        repository.update(account); QTRY_COMPARE(saved.size(), 2); QTRY_VERIFY(!loaded.isEmpty());
        auto values = qvariant_cast<QList<Account>>(loaded.takeLast().first());
        QCOMPARE(values.first().id, account.id); QCOMPARE(values.first().imap.host, account.imap.host);
        loaded.clear(); repository.remove(account.id); QTRY_COMPARE(removed.size(), 1); QTRY_VERIFY(!loaded.isEmpty());
        QVERIFY(qvariant_cast<QList<Account>>(loaded.takeLast().first()).isEmpty());
        repository.update(account); QTRY_COMPARE(failed.size(), 1);
    }
    void validationAndJson()
    {
        const auto original = makeAccount(QStringLiteral("user@example.test"));
        QVERIFY(validateAccount(original).isEmpty());
        Account restored;
        QVERIFY(accountFromJson(accountJson(original), restored));
        QCOMPARE(restored.id, original.id);
        QCOMPARE(restored.imap.port, original.imap.port);
        QCOMPARE(restored.smtp.encryption, EncryptionMode::ImplicitTls);
        auto bad = original;
        bad.imap.encryption = static_cast<EncryptionMode>(99);
        QVERIFY(!validateAccount(bad).isEmpty());
        bad = original;
        bad.smtp.host = QStringLiteral("smtp.test\r\nINJECT");
        QVERIFY(!validateAccount(bad).isEmpty());
        auto json = accountJson(original);
        auto server = json.value("imap").toObject();
        server.remove("encryption");
        json.insert("imap", server);
        QVERIFY(!accountFromJson(json, restored));
        server.insert("encryption", 0.5);
        json.insert("imap", server);
        QVERIFY(!accountFromJson(json, restored));
    }

    void persistsAndReordersWithoutSecrets()
    {
        const auto directory = QDir::currentPath() + QStringLiteral("/account-tests-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(QDir().mkpath(directory));
        const auto path = directory + QStringLiteral("/accounts.sqlite");
        const auto first = makeAccount(QStringLiteral("first@example.test"));
        const auto second = makeAccount(QStringLiteral("second@example.test"));
        {
            AccountRepository repository(path);
            QSignalSpy loaded(&repository, &AccountRepository::loaded);
            QSignalSpy errors(&repository, &AccountRepository::failed);
            QTRY_VERIFY(!loaded.isEmpty());
            QVERIFY(qvariant_cast<QList<Account>>(loaded.takeLast().first()).isEmpty());
            repository.insert(first);
            repository.insert(second);
            QTRY_VERIFY(loaded.size() >= 2);
            auto accounts = qvariant_cast<QList<Account>>(loaded.takeLast().first());
            QCOMPARE(accounts.size(), 2);
            loaded.clear();
            repository.reorder({second.id, first.id});
            QTRY_VERIFY(!loaded.isEmpty());
            accounts = qvariant_cast<QList<Account>>(loaded.takeLast().first());
            QCOMPARE(accounts.first().id, second.id);
            repository.reorder({first.id, first.id});
            QTRY_COMPARE(errors.size(), 1);
            repository.insert(first); // Duplicate IDs must not overwrite configuration.
            QTRY_COMPARE(errors.size(), 2);
        }
        {
            AccountRepository repository(path);
            QSignalSpy loaded(&repository, &AccountRepository::loaded);
            QTRY_VERIFY(!loaded.isEmpty());
            const auto accounts = qvariant_cast<QList<Account>>(loaded.takeLast().first());
            QCOMPARE(accounts.size(), 2);
            QCOMPARE(accounts.first().id, second.id);
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto data = file.readAll();
        QVERIFY(!data.contains("password"));
        QVERIFY(!data.contains("refresh_token"));
        file.close();
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().rmdir(directory));
    }

    void reportsOpenFailure()
    {
        AccountRepository repository(QDir::currentPath()); // An existing directory is not a database file.
        QSignalSpy errors(&repository, &AccountRepository::failed);
        QTRY_VERIFY(!errors.isEmpty());
    }
    void snapshotsReplaceUidValidityAtomically()
    {
        AccountRepository repository(QStringLiteral(":memory:"));
        QSignalSpy inserted(&repository, &AccountRepository::inserted);
        QSignalSpy saved(&repository, &AccountRepository::snapshotSaved);
        QSignalSpy loaded(&repository, &AccountRepository::cacheLoaded);
        QSignalSpy failed(&repository, &AccountRepository::failed);
        const auto account = makeAccount(QStringLiteral("cache@example.test"));
        repository.insert(account);
        QTRY_COMPARE(inserted.size(), 1);
        const auto parsed = MimeParser::parse("From: test@example.test\r\nSubject: cached\r\n\r\nbody\r\n");
        QVERIFY(parsed.ok());
        const QList<MailFolder> folders{{QStringLiteral("INBOX"), u'/', {}, 1}};
        MailRecord record{account.id, QStringLiteral("INBOX"), 10, 1, false, true, parsed.message};
        repository.saveSnapshot(account.id, record.folder, folders, {record});
        QTRY_COMPARE(saved.size(), 1);
        repository.loadCache(account.id);
        QTRY_COMPARE(loaded.size(), 1);
        auto cached = qvariant_cast<QList<MailRecord>>(loaded.last().at(3));
        QCOMPARE(cached.first().uidValidity, 10);
        QCOMPARE(cached.first().document.rawSource, parsed.message.rawSource);
        record.uidValidity = 20; record.uid = 9;
        repository.saveSnapshot(account.id, record.folder, folders, {record});
        QTRY_COMPARE(saved.size(), 2);
        record.uid = -1;
        repository.saveSnapshot(account.id, record.folder, folders, {record});
        QTRY_COMPARE(failed.size(), 1);
        repository.loadCache(account.id);
        QTRY_COMPARE(loaded.size(), 2);
        cached = qvariant_cast<QList<MailRecord>>(loaded.last().at(3));
        QCOMPARE(cached.size(), 1);
        QCOMPARE(cached.first().uidValidity, 20);
        QCOMPARE(cached.first().uid, 9);
        QVERIFY(cached.first().important);
        QVERIFY(!cached.first().seen);
    }
    void savesAttachmentsWithoutTraversalOrOverwrite()
    {
        const auto directory = QDir::currentPath() + QStringLiteral("/download-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        AccountRepository repository(QStringLiteral(":memory:"));
        QSignalSpy saved(&repository, &AccountRepository::attachmentSaved);
        QSignalSpy history(&repository, &AccountRepository::historyLoaded);
        const auto parsed = MimeParser::parse(
            "From: sender@example.test\r\nSubject: attachment\r\nMIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=x\r\n\r\n"
            "--x\r\nContent-Type: text/plain\r\n\r\nbody\r\n--x\r\nContent-Type: application/octet-stream\r\n"
            "Content-Disposition: attachment; filename=\"../../test.bin\"\r\nContent-Transfer-Encoding: base64\r\n\r\nAAEC\r\n--x--\r\n");
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.message.attachments.size(), 1);
        MailRecord record{QStringLiteral("a"), QStringLiteral("INBOX"), 10, 1, false, false, parsed.message};
        const auto part = parsed.message.attachments.first().partId;
        repository.saveAttachment(record, part, directory);
        QTRY_COMPARE(saved.size(), 1);
        QVERIFY2(saved.first().at(1).toString().isEmpty(), qPrintable(saved.first().at(1).toString()));
        const auto first = qvariant_cast<DownloadEntry>(saved.first().first());
        QCOMPARE(QFileInfo(first.path).absolutePath(), QFileInfo(directory).absoluteFilePath());
        QFile file(first.path); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("\0\1\2", 3)); file.close();
        repository.saveAttachment(record, part, directory);
        QTRY_COMPARE(saved.size(), 2);
        const auto second = qvariant_cast<DownloadEntry>(saved.last().first());
        QVERIFY(first.path != second.path);
        QVERIFY(QFile::exists(first.path));
        repository.loadDownloadHistory(); QTRY_COMPARE(history.size(), 1);
        const auto entries = qvariant_cast<QList<DownloadEntry>>(history.first().first());
        QCOMPARE(entries.size(), 2); QCOMPARE(entries.first().size, 3);
        QCOMPARE(entries.first().mimeType, QStringLiteral("application/octet-stream"));
        QCOMPARE(entries.first().uidValidity, 10);
        repository.saveAttachment(record, QStringLiteral("1.9999"), directory);
        QTRY_COMPARE(saved.size(), 3);
        QVERIFY(!saved.last().at(1).toString().isEmpty());
        QCOMPARE(safeDownloadName(QStringLiteral("CON.txt")), QStringLiteral("_CON.txt"));
        QVERIFY(!safeDownloadName(QStringLiteral("..\\evil:ads/file")).contains(u'\\'));
        QVERIFY(QFile::remove(first.path)); QVERIFY(QFile::remove(second.path)); QVERIFY(QDir().rmdir(directory));
    }
};
QTEST_GUILESS_MAIN(AccountRepositoryTest)
#include "tst_accountrepository.moc"
