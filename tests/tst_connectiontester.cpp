#include "mail/connectiontester.h"
#include "mailtestserver.h"
#include <QSignalSpy>
#include <QFile>
#include <QSslConfiguration>
#include <QUuid>
#include <QtTest>

using namespace smoothmail;
class ConnectionTesterTest final : public QObject
{
    Q_OBJECT
private slots:
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
private:
    QSslConfiguration m_defaultSsl;
};
QTEST_GUILESS_MAIN(ConnectionTesterTest)
#include "tst_connectiontester.moc"
