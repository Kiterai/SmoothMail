#include "storage/accountrepository.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>
#include "mail/mimeparser.h"

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
};
QTEST_GUILESS_MAIN(AccountRepositoryTest)
#include "tst_accountrepository.moc"
