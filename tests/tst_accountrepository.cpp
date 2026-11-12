#include "storage/accountrepository.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

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
};
QTEST_GUILESS_MAIN(AccountRepositoryTest)
#include "tst_accountrepository.moc"
