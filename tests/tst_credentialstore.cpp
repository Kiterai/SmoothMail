#include "storage/credentialstore.h"
#include "storage/accountregistrar.h"
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

class CredentialStoreTest final : public QObject
{
    Q_OBJECT
signals:
    void completed(const QString &value, const QString &error);
private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("SMOOTHMAIL_TEST_KEYCHAIN") != QStringLiteral("1")) {
            QSKIP("OS credential test requires explicit opt-in; run only in the disposable guest.");
        }
        m_store = new smoothmail::CredentialStore(this,
            QStringLiteral("SmoothMail-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    void roundTrip_data()
    {
        QTest::addColumn<QString>("secret");
        QTest::newRow("unicode") << QStringLiteral("日本語 dummy secret");
        QTest::newRow("long-token") << QString(8192, u'x');
    }
    void roundTrip()
    {
        QFETCH(QString, secret);
        QSignalSpy finished(this, &CredentialStoreTest::completed);
        const auto callback = [this](const QString &value, const QString &error) { emit completed(value, error); };
        m_store->write(QStringLiteral("disposable"), secret, callback);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished.first().at(1).toString().isEmpty(), qPrintable(finished.first().at(1).toString()));
        finished.clear();
        m_store->read(QStringLiteral("disposable"), callback);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished.first().at(1).toString().isEmpty(), qPrintable(finished.first().at(1).toString()));
        QCOMPARE(finished.first().first().toString(), secret);
        finished.clear();
        m_store->remove(QStringLiteral("disposable"), callback);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(finished.first().at(1).toString().isEmpty());
        finished.clear();
        m_store->read(QStringLiteral("disposable"), callback);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(!finished.first().at(1).toString().isEmpty());
        QVERIFY(finished.first().first().toString().isEmpty());
    }
    void registrationRollsBackOnStorageFailure()
    {
        smoothmail::AccountRepository repository(QStringLiteral(":memory:"));
        smoothmail::AccountRegistrar registrar(&repository, m_store);
        QSignalSpy saved(&registrar, &smoothmail::AccountRegistrar::finished);
        QSignalSpy loaded(&repository, &smoothmail::AccountRepository::loaded);
        QSignalSpy errors(&repository, &smoothmail::AccountRepository::failed);
        QTRY_VERIFY(!loaded.isEmpty() || !errors.isEmpty());
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().at(1).toString()));
        smoothmail::Account account{QString{}, QStringLiteral("test@example.test"),
            {QStringLiteral("smtp.test"), 465, QStringLiteral("test")},
            {QStringLiteral("imap.test"), 993, QStringLiteral("test")}};
        // Larger than the Windows backend limit. The prior IMAP write must
        // be removed, and no account configuration may be published.
        registrar.add(account, QString(50000, u'x'), QStringLiteral("dummy imap secret"));
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        QVERIFY(!saved.first().at(1).toString().isEmpty());
        const auto id = saved.first().first().toString();
        QVERIFY(!id.isEmpty());
        QSignalSpy read(this, &CredentialStoreTest::completed);
        m_store->read(smoothmail::AccountRegistrar::secretKey(id, QStringLiteral("imap")),
            [this](const QString &value, const QString &error) { emit completed(value, error); });
        QTRY_COMPARE_WITH_TIMEOUT(read.size(), 1, 10000);
        QVERIFY(!read.first().at(1).toString().isEmpty());
        QVERIFY(read.first().first().toString().isEmpty());
        loaded.clear();
        repository.reload();
        QTRY_VERIFY(!loaded.isEmpty());
        QVERIFY(qvariant_cast<QList<smoothmail::Account>>(loaded.last().first()).isEmpty());
        saved.clear();
        loaded.clear();
        registrar.add(account, QStringLiteral("dummy smtp secret"), QStringLiteral("dummy imap secret"));
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        QVERIFY2(saved.first().at(1).toString().isEmpty(), qPrintable(saved.first().at(1).toString()));
        QTRY_VERIFY(!loaded.isEmpty());
        QCOMPARE(qvariant_cast<QList<smoothmail::Account>>(loaded.last().first()).size(), 1);
        for (const auto &protocol : {QStringLiteral("imap"), QStringLiteral("smtp")}) {
            read.clear();
            m_store->remove(smoothmail::AccountRegistrar::secretKey(saved.first().first().toString(), protocol),
                [this](const QString &value, const QString &error) { emit completed(value, error); });
            QTRY_COMPARE_WITH_TIMEOUT(read.size(), 1, 10000);
            QVERIFY(read.first().at(1).toString().isEmpty());
        }
    }
private:
    smoothmail::CredentialStore *m_store = nullptr;
};
QTEST_GUILESS_MAIN(CredentialStoreTest)
#include "tst_credentialstore.moc"
