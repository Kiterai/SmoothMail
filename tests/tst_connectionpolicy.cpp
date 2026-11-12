#include "mail/connectionpolicy.h"

#include <QtTest>

using namespace smoothmail;

class ConnectionPolicyTest final : public QObject
{
    Q_OBJECT

private slots:
    void requiresEstablishedAndVerifiedTls_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("active");
        QTest::addColumn<bool>("verified");
        QTest::addColumn<bool>("allowed");
        for (const auto mode : {EncryptionMode::ImplicitTls, EncryptionMode::StartTls}) {
            const auto name = mode == EncryptionMode::StartTls ? QByteArray("starttls") : QByteArray("tls");
            QTest::newRow((name + "-not-started").constData()) << int(mode) << false << false << false;
            QTest::newRow((name + "-stale-verification").constData()) << int(mode) << false << true << false;
            QTest::newRow((name + "-bad-certificate").constData()) << int(mode) << true << false << false;
            QTest::newRow((name + "-verified").constData()) << int(mode) << true << true << true;
        }
    }

    void requiresEstablishedAndVerifiedTls()
    {
        QFETCH(int, mode);
        QFETCH(bool, active);
        QFETCH(bool, verified);
        QFETCH(bool, allowed);
        const auto result = authenticationDecision(static_cast<EncryptionMode>(mode),
                                                   AuthenticationMode::Plain, {active, verified},
                                                   {QStringLiteral("PLAIN"), QStringLiteral("LOGIN")});
        QCOMPARE(result.allowed(), allowed);
        if (!allowed) {
            QVERIFY(result.mechanism.isEmpty());
            QVERIFY(!result.error.isEmpty());
        }
    }

    void permitsOnlyExplicitUnencryptedMode()
    {
        const auto result = authenticationDecision(EncryptionMode::None, AuthenticationMode::Login,
                                                   {}, {QStringLiteral("LOGIN")});
        QVERIFY(result.allowed());
        QCOMPARE(result.mechanism, QStringLiteral("LOGIN"));
    }

    void refusesBadCertificateEvenWhenNoneWasSelected()
    {
        QVERIFY(!authenticationDecision(EncryptionMode::None, AuthenticationMode::Plain,
                                         {true, false}, {QStringLiteral("PLAIN")}).allowed());
    }

    void neverFallsBackFromOAuth()
    {
        const auto result = authenticationDecision(EncryptionMode::StartTls, AuthenticationMode::OAuth2,
                                                   {true, true},
                                                   {QStringLiteral("PLAIN"), QStringLiteral("LOGIN")});
        QVERIFY(!result.allowed());
        QVERIFY(result.mechanism.isEmpty());
        QVERIFY(result.error.contains(QStringLiteral("XOAUTH2")));
    }

    void selectsOnlyConfiguredMechanism()
    {
        const QStringList advertised = {QStringLiteral("LOGIN"), QStringLiteral("PLAIN"),
                                        QStringLiteral("XOAUTH2")};
        for (const auto mode : {AuthenticationMode::Plain, AuthenticationMode::Login, AuthenticationMode::OAuth2}) {
            const auto result = authenticationDecision(EncryptionMode::ImplicitTls, mode,
                                                       {true, true}, advertised);
            QVERIFY(result.allowed());
            const auto expected = mode == AuthenticationMode::Plain ? QStringLiteral("PLAIN")
                : mode == AuthenticationMode::Login ? QStringLiteral("LOGIN") : QStringLiteral("XOAUTH2");
            QCOMPARE(result.mechanism, expected);
        }
    }

    void rejectsNoncanonicalMechanisms()
    {
        for (const auto &advertised : {QStringList{QStringLiteral("xoauth2")},
                                      QStringList{QStringLiteral("XOAUTH2-OTHER")},
                                      QStringList{QStringLiteral(" XOAUTH2")}, QStringList{}}) {
            QVERIFY(!authenticationDecision(EncryptionMode::ImplicitTls, AuthenticationMode::OAuth2,
                                             {true, true}, advertised).allowed());
        }
    }

    void rejectsUnknownEnums()
    {
        QVERIFY(!authenticationDecision(static_cast<EncryptionMode>(99), AuthenticationMode::Plain,
                                         {true, true}, {QStringLiteral("PLAIN")}).allowed());
        QVERIFY(!authenticationDecision(EncryptionMode::ImplicitTls, static_cast<AuthenticationMode>(99),
                                         {true, true}, {QStringLiteral("PLAIN")}).allowed());
    }
};

QTEST_GUILESS_MAIN(ConnectionPolicyTest)
#include "tst_connectionpolicy.moc"
