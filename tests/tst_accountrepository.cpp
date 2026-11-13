#include "storage/accountrepository.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>
#include "mail/mimeparser.h"
#include "storage/downloadfile.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

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
