#include "mail/connectionpolicy.h"
#include "mailtestserver.h"

#include <KIMAP/CapabilitiesJob>
#include <KIMAP/LoginJob>
#include <KIMAP/Session>
#include <KLocalizedString>
#include <KSMTP/LoginJob>
#include <KSMTP/Session>
#include <QLoggingCategory>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QtTest>

extern "C" {
#include <sasl/sasl.h>
}

Q_DECLARE_METATYPE(KIMAP::Session::State)

using namespace smoothmail;

namespace {
const QString user = QStringLiteral("test@example.com");
const QString secret = QStringLiteral("dummy-test-secret");
const QStringList allMechanisms = {QStringLiteral("PLAIN"), QStringLiteral("LOGIN"), QStringLiteral("XOAUTH2")};

void checkResponse(const MailTestServer &server, AuthenticationMode mode)
{
    QVERIFY(!server.authenticationResponses.isEmpty());
    if (mode == AuthenticationMode::OAuth2) {
        QCOMPARE(server.authenticationResponses.last(), QByteArray("user=test@example.com\1auth=Bearer dummy-test-secret\1\1"));
    } else if (mode == AuthenticationMode::Plain) {
        const auto fields = server.authenticationResponses.last().split('\0');
        QCOMPARE(fields.size(), 3);
        QCOMPARE(fields.at(1), user.toUtf8());
        QCOMPARE(fields.at(2), secret.toUtf8());
    } else {
        QCOMPARE(server.authenticationResponses.size(), 2);
        QCOMPARE(server.authenticationResponses.at(0), user.toUtf8());
        QCOMPARE(server.authenticationResponses.at(1), secret.toUtf8());
    }
}

KSmtp::LoginJob::AuthMode smtpMode(AuthenticationMode mode)
{
    switch (mode) {
    case AuthenticationMode::Plain: return KSmtp::LoginJob::Plain;
    case AuthenticationMode::Login: return KSmtp::LoginJob::Login;
    case AuthenticationMode::OAuth2: return KSmtp::LoginJob::XOAuth2;
    }
    Q_UNREACHABLE();
}

KIMAP::LoginJob::AuthenticationMode imapMode(AuthenticationMode mode)
{
    switch (mode) {
    case AuthenticationMode::Plain: return KIMAP::LoginJob::Plain;
    case AuthenticationMode::Login: return KIMAP::LoginJob::Login;
    case AuthenticationMode::OAuth2: return KIMAP::LoginJob::XOAuth2;
    }
    Q_UNREACHABLE();
}
}

class MailTransportTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        auto plugins = QDir::toNativeSeparators(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")).toUtf8();
        QVERIFY(!plugins.isEmpty());
        QCOMPARE(sasl_set_path(SASL_PATH_TYPE_PLUGIN, plugins.data()), SASL_OK);
        qRegisterMetaType<KIMAP::Session::State>();
        KLocalizedString::setApplicationDomain("smoothmail-transport-test");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
        QLoggingCategory::setFilterRules(QStringLiteral("org.kde.pim.*.debug=false"));
        // Fail early rather than timing out each protocol case when the test
        // environment prohibits loopback TCP (for example WinError 10013).
        verifyLoopbackFixture();
    }

    void smtpAuthentication_data() { authenticationRows(); }
    void imapAuthentication_data() { authenticationRows(); }

private:
    void verifyLoopbackFixture()
    {
        MailTestServer server(MailTestServer::Protocol::Smtp, allMechanisms);
        QVERIFY(server.listen());
        QTcpSocket socket;
        socket.setProxy(QNetworkProxy::NoProxy);
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_VERIFY2_WITH_TIMEOUT(socket.state() == QAbstractSocket::ConnectedState,
                                 qPrintable(QStringLiteral("Loopback TCP unavailable: %1").arg(socket.errorString())), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(), 5000);
        QCOMPARE(socket.readLine(), QByteArray("220 localhost ESMTP\r\n"));
    }

private slots:
    void smtpAuthentication()
    {
        QFETCH(int, authentication);
        const auto mode = static_cast<AuthenticationMode>(authentication);
        MailTestServer server(MailTestServer::Protocol::Smtp, allMechanisms);
        QVERIFY(server.listen());
        KSmtp::Session session(QStringLiteral("127.0.0.1"), server.port());
        session.setUseNetworkProxy(false);
        session.setSocketTimeout(2000);
        session.setEncryptionMode(KSmtp::Session::Unencrypted);
        session.open();
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), KSmtp::Session::NotAuthenticated, 5000);
        const auto decision = authenticationDecision(EncryptionMode::None, mode, {}, session.availableAuthModes());
        QVERIFY2(decision.allowed(), qPrintable(decision.error));
        auto *job = new KSmtp::LoginJob(&session);
        job->setUserName(user);
        job->setPassword(secret);
        job->setPreferedAuthMode(smtpMode(mode));
        job->setAutoDelete(false);
        QSignalSpy result(job, &KJob::result);
        job->start();
        QTRY_COMPARE_WITH_TIMEOUT(result.count(), 1, 5000);
        QCOMPARE(job->error(), 0);
        QCOMPARE(job->usedAuthMode(), smtpMode(mode));
        QCOMPARE(session.state(), KSmtp::Session::Authenticated);
        checkResponse(server, mode);
        delete job;
        session.quit();
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), KSmtp::Session::Disconnected, 5000);
    }

    void imapAuthentication()
    {
        QFETCH(int, authentication);
        const auto mode = static_cast<AuthenticationMode>(authentication);
        MailTestServer server(MailTestServer::Protocol::Imap, allMechanisms);
        QVERIFY(server.listen());
        KIMAP::Session session(QStringLiteral("127.0.0.1"), server.port());
        session.setUseNetworkProxy(false);
        session.setTimeout(2);
        auto *capabilities = new KIMAP::CapabilitiesJob(&session);
        capabilities->setAutoDelete(false);
        QSignalSpy capabilityResult(capabilities, &KJob::result);
        capabilities->start();
        QTRY_COMPARE_WITH_TIMEOUT(capabilityResult.count(), 1, 5000);
        QCOMPARE(capabilities->error(), 0);
        QStringList mechanisms;
        for (const auto &capability : capabilities->capabilities()) {
            if (capability.startsWith(QStringLiteral("AUTH="))) {
                mechanisms.append(capability.mid(5));
            }
        }
        delete capabilities;
        const auto decision = authenticationDecision(EncryptionMode::None, mode, {}, mechanisms);
        QVERIFY2(decision.allowed(), qPrintable(decision.error));
        auto *job = new KIMAP::LoginJob(&session);
        job->setAutoDelete(false);
        job->setUserName(user);
        job->setPassword(secret);
        job->setEncryptionMode(KIMAP::LoginJob::Unencrypted);
        job->setAuthenticationMode(imapMode(mode));
        QSignalSpy result(job, &KJob::result);
        job->start();
        QTRY_COMPARE_WITH_TIMEOUT(result.count(), 1, 5000);
        QCOMPARE(job->error(), 0);
        QVERIFY2(session.state() == KIMAP::Session::Authenticated, qPrintable(job->errorText()));
        checkResponse(server, mode);
        delete job;
        session.close();
    }

    void smtpDoesNotFallBackFromOAuth()
    {
        MailTestServer server(MailTestServer::Protocol::Smtp,
                              {QStringLiteral("PLAIN"), QStringLiteral("LOGIN")});
        QVERIFY(server.listen());
        KSmtp::Session session(QStringLiteral("127.0.0.1"), server.port());
        session.setUseNetworkProxy(false);
        session.setEncryptionMode(KSmtp::Session::Unencrypted);
        session.setSocketTimeout(2000);
        session.open();
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), KSmtp::Session::NotAuthenticated, 5000);
        const auto decision = authenticationDecision(EncryptionMode::None, AuthenticationMode::OAuth2,
                                                     {}, session.availableAuthModes());
        QVERIFY(!decision.allowed());
        // Deliberately do not construct LoginJob or supply the secret on rejection.
        session.quit();
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), KSmtp::Session::Disconnected, 5000);
        QVERIFY(!server.receivedAuthentication());
        QVERIFY(server.authenticationResponses.isEmpty());
    }

    void smtpMissingStartTlsNeverAuthenticates()
    {
        MailTestServer server(MailTestServer::Protocol::Smtp, allMechanisms);
        QVERIFY(server.listen());
        KSmtp::Session session(QStringLiteral("127.0.0.1"), server.port());
        session.setUseNetworkProxy(false);
        session.setEncryptionMode(KSmtp::Session::STARTTLS);
        session.setSocketTimeout(2000);
        session.open();
        // Upstream quits without emitting connectionError for missing STARTTLS.
        QTRY_VERIFY_WITH_TIMEOUT(server.commands.contains(QByteArray("QUIT")), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), KSmtp::Session::Disconnected, 5000);
        QVERIFY(!server.receivedAuthentication());
    }

    void imapMissingStartTlsNeverAuthenticates()
    {
        MailTestServer server(MailTestServer::Protocol::Imap, allMechanisms);
        QVERIFY(server.listen());
        KIMAP::Session session(QStringLiteral("127.0.0.1"), server.port());
        session.setUseNetworkProxy(false);
        session.setTimeout(2);
        auto *job = new KIMAP::LoginJob(&session);
        job->setAutoDelete(false);
        job->setUserName(user);
        job->setPassword(secret);
        job->setAuthenticationMode(KIMAP::LoginJob::XOAuth2);
        job->setEncryptionMode(KIMAP::LoginJob::STARTTLS);
        QSignalSpy result(job, &KJob::result);
        job->start();
        QTRY_COMPARE_WITH_TIMEOUT(result.count(), 1, 5000);
        QVERIFY(job->error() != 0);
        QVERIFY(!server.receivedAuthentication());
        QVERIFY(server.authenticationResponses.isEmpty());
        delete job;
        session.close();
    }

private:
    static void authenticationRows()
    {
        QTest::addColumn<int>("authentication");
        QTest::newRow("PLAIN") << int(AuthenticationMode::Plain);
        QTest::newRow("LOGIN") << int(AuthenticationMode::Login);
        QTest::newRow("XOAUTH2") << int(AuthenticationMode::OAuth2);
    }
};

QTEST_GUILESS_MAIN(MailTransportTest)
#include "tst_mailtransport.moc"
