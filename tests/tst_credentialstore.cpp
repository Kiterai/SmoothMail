#include "storage/credentialstore.h"
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
private:
    smoothmail::CredentialStore *m_store = nullptr;
};
QTEST_GUILESS_MAIN(CredentialStoreTest)
#include "tst_credentialstore.moc"
