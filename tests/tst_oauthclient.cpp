#include "mail/oauthclient.h"
#include "mail/authenticationbroker.h"
#include "storage/credentialstore.h"
#include "storage/accountregistrar.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>
#include <cstring>
#include <QUuid>

using namespace smoothmail;
class FakeReply final : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, QByteArray body, int status, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(request); setUrl(request.url()); setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this] { setFinished(true); emit readyRead(); emit finished(); });
    }
    void abort() override { setFinished(true); }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = qMin(maximum, m_body.size() - m_offset);
        if (!count) return -1;
        std::memcpy(data, m_body.constData() + m_offset, size_t(count)); m_offset += count; return count;
    }
private:
    QByteArray m_body;
    qint64 m_offset = 0;
};
class FakeNetwork final : public QNetworkAccessManager
{
public:
    QJsonObject response{{"access_token", "dummy_access"}, {"refresh_token", "dummy_refresh"},
        {"expires_in", 3600}, {"token_type", "Bearer"}, {"scope", "https://mail.google.com/"}};
    int status = 200;
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *body) override
    {
        requests.append(request); bodies.append(body ? body->readAll() : QByteArray{});
        return new FakeReply(request, QJsonDocument(response).toJson(QJsonDocument::Compact), status, this);
    }
};

class OAuthClientTest final : public QObject
{
    Q_OBJECT
signals:
    void credentialCompleted(const QString &value, const QString &error);
private:
    static void callback(const QUrl &redirect, const QByteArray &query)
    {
        QTcpSocket socket; socket.setProxy(QNetworkProxy::NoProxy);
        socket.connectToHost(QHostAddress::LocalHost, quint16(redirect.port()));
        QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QAbstractSocket::ConnectedState, 3000);
        socket.write("GET /?" + query + " HTTP/1.1\r\nHost: " + redirect.host().toLatin1() + ':'
                     + QByteArray::number(redirect.port()) + "\r\n\r\n");
        QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QAbstractSocket::UnconnectedState, 3000);
    }
private slots:
    void pkceAndGrantEncoding()
    {
        QCOMPARE(pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), QByteArray("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
        QCOMPARE(refreshTokenFromGrant(refreshGrant(QStringLiteral("dummy-refresh"))), QStringLiteral("dummy-refresh"));
        QVERIFY(refreshTokenFromGrant(QStringLiteral("ordinary-password")).isEmpty());
        QVERIFY(refreshGrant(QStringLiteral("injected\r\ntoken")).isEmpty());
    }
    void authorizationChecksStateAndUsesPkce()
    {
        FakeNetwork network; OAuthClient client(nullptr, &network);
        QSignalSpy url(&client, &OAuthClient::authorizationUrlReady);
        QSignalSpy result(&client, &OAuthClient::tokensReady);
        QSignalSpy errors(&client, &OAuthClient::failed);
        QSignalSpy received(&client, &OAuthClient::authorizationCodeReceived);
        client.authorize(QStringLiteral("google"), QStringLiteral("dummy-client"), QStringLiteral("test+tag@example.test"));
        QCOMPARE(url.size(), 1);
        const auto authorization = url.first().first().toUrl();
        const QUrlQuery parameters(authorization);
        QCOMPARE(authorization.host(), QStringLiteral("accounts.google.com"));
        QCOMPARE(parameters.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
        const auto state = parameters.queryItemValue(QStringLiteral("state"));
        QVERIFY(state.size() >= 43);
        const QUrl redirect(parameters.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
        QCOMPARE(redirect.host(), QStringLiteral("127.0.0.1"));
        callback(redirect, "state=wrong&code=dummy");
        QCOMPARE(network.requests.size(), 0); QVERIFY(result.isEmpty()); QVERIFY(errors.isEmpty()); QVERIFY(received.isEmpty());
        const auto encodedState = QUrl::toPercentEncoding(state);
        callback(redirect, "state=" + encodedState + "&state=" + encodedState + "&code=dummy");
        QCOMPARE(network.requests.size(), 0);
        callback(redirect, "state=" + encodedState + "&code=dummy%2Bcode");
        QTRY_COMPARE(result.size(), 1);
        QCOMPARE(received.size(), 1);
        QVERIFY(errors.isEmpty());
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(network.requests.first().url(), QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
        QCOMPARE(network.requests.first().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        const QUrlQuery posted(QString::fromLatin1(network.bodies.first()));
        const auto verifier = posted.queryItemValue(QStringLiteral("code_verifier"), QUrl::FullyDecoded).toLatin1();
        QCOMPARE(QString::fromLatin1(pkceChallenge(verifier)), parameters.queryItemValue(QStringLiteral("code_challenge")));
        QCOMPARE(posted.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded), QStringLiteral("dummy+code"));
        const auto tokens = qvariant_cast<OAuthTokens>(result.first().first());
        QCOMPARE(tokens.refreshToken, QStringLiteral("dummy_refresh"));
        QVERIFY(tokens.expiresAt > QDateTime::currentDateTimeUtc());
    }
    void refreshRotationRevocationAndRedirectRejection()
    {
        FakeNetwork network; OAuthClient client(nullptr, &network);
        QSignalSpy result(&client, &OAuthClient::tokensReady); QSignalSpy errors(&client, &OAuthClient::failed);
        client.refresh(QStringLiteral("google"), QStringLiteral("id"), QStringLiteral("old_refresh"));
        QTRY_COMPARE(result.size(), 1);
        QCOMPARE(qvariant_cast<OAuthTokens>(result.takeFirst().first()).refreshToken, QStringLiteral("dummy_refresh"));
        network.response.remove("refresh_token");
        client.refresh(QStringLiteral("google"), QStringLiteral("id"), QStringLiteral("old_refresh"));
        QTRY_COMPARE(result.size(), 1);
        QCOMPARE(qvariant_cast<OAuthTokens>(result.takeFirst().first()).refreshToken, QStringLiteral("old_refresh"));
        network.status = 400; network.response = {{"error", "invalid_grant"}};
        client.refresh(QStringLiteral("google"), QStringLiteral("id"), QStringLiteral("revoked"));
        QTRY_COMPARE(errors.size(), 1); QVERIFY(result.isEmpty());
        QVERIFY(errors.first().first().toString().contains(QStringLiteral("再認可")));
        network.status = 302; network.response = {{"access_token", "must-not-accept"}};
        client.refresh(QStringLiteral("google"), QStringLiteral("id"), QStringLiteral("refresh"));
        QTRY_COMPARE(errors.size(), 2); QVERIFY(result.isEmpty());
    }
    void reportsRegistrationErrorsWithoutEchoingProviderData()
    {
        FakeNetwork network; OAuthClient client(nullptr, &network);
        QSignalSpy errors(&client, &OAuthClient::failed);
        network.status = 400;
        for (const auto &provider : {QStringLiteral("google"), QStringLiteral("microsoft")}) {
            for (const auto &code : {QStringLiteral("invalid_client"), QStringLiteral("unauthorized_client"),
                                    QStringLiteral("invalid_request"), QStringLiteral("invalid_scope")}) {
                network.response = {{"error", code}, {"error_description", "must-not-echo-token-or-request"}};
                client.refresh(provider, QStringLiteral("id"), QStringLiteral("dummy_refresh"));
                QTRY_COMPARE(errors.size(), 1);
                const auto error = errors.takeFirst().first().toString();
                QVERIFY(error.contains(code));
                QVERIFY(!error.contains(QStringLiteral("must-not-echo")));
                if (provider == QStringLiteral("microsoft")) {
                    QVERIFY(!error.contains(QStringLiteral("Google"), Qt::CaseInsensitive));
                    if (code == QStringLiteral("invalid_client")) {
                        QVERIFY(error.contains(QStringLiteral("Microsoft")));
                        QVERIFY(error.contains(QStringLiteral("モバイル アプリケーションとデスクトップ アプリケーション")));
                        QVERIFY(error.contains(QStringLiteral("http://localhost")));
                        QVERIFY(error.contains(QStringLiteral("SMOOTHMAIL_MICROSOFT_CLIENT_ID")));
                        QVERIFY(error.contains(QStringLiteral("クライアントシークレットは不要")));
                    }
                } else if (code == QStringLiteral("invalid_client")) {
                    QVERIFY(error.contains(QStringLiteral("Google")));
                    QVERIFY(error.contains(QStringLiteral("Desktop app")));
                    QVERIFY(error.contains(QStringLiteral("client_secret")));
                    QVERIFY(!error.contains(QStringLiteral("Microsoft")));
                }
            }
        }
        network.response = {{"error", "invalid_request"}, {"error_description", "client_secret is missing."}};
        for (const auto &provider : {QStringLiteral("google"), QStringLiteral("microsoft")}) {
            client.refresh(provider, QStringLiteral("id"), QStringLiteral("dummy_refresh"));
            QTRY_COMPARE(errors.size(), 1);
            const auto error = errors.takeFirst().first().toString();
            if (provider == QStringLiteral("google")) {
                QVERIFY(error.contains(QStringLiteral("SMOOTHMAIL_GOOGLE_CLIENT_SECRET")));
            } else {
                QVERIFY(error.contains(QStringLiteral("invalid_request")));
                QVERIFY(!error.contains(QStringLiteral("Google"), Qt::CaseInsensitive));
                QVERIFY(!error.contains(QStringLiteral("client_secret")));
            }
        }
    }
    void microsoftAndCancellation()
    {
        FakeNetwork network; OAuthClient client(nullptr, &network);
        QSignalSpy url(&client, &OAuthClient::authorizationUrlReady); QSignalSpy errors(&client, &OAuthClient::failed);
        client.authorize(QStringLiteral("microsoft"), QStringLiteral("id"), {});
        const auto authorization = url.first().first().toUrl();
        QCOMPARE(authorization.host(), QStringLiteral("login.microsoftonline.com"));
        const QUrlQuery parameters(authorization);
        QVERIFY(parameters.queryItemValue(QStringLiteral("scope"), QUrl::FullyDecoded).contains(QStringLiteral("SMTP.Send")));
        QCOMPARE(QUrl(parameters.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded)).host(), QStringLiteral("localhost"));
        const auto firstState = parameters.queryItemValue(QStringLiteral("state"));
        client.cancel(); QCOMPARE(errors.size(), 1); QVERIFY(network.requests.isEmpty());
        client.authorize(QStringLiteral("microsoft"), QStringLiteral("id"), {});
        QVERIFY(QUrlQuery(url.last().first().toUrl()).queryItemValue(QStringLiteral("state")) != firstState);
        network.response["scope"] = QStringLiteral("IMAP.AccessAsUser.All SMTP.Send");
        QSignalSpy tokens(&client, &OAuthClient::tokensReady);
        const QUrlQuery second(url.last().first().toUrl());
        callback(QUrl(second.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded)),
            "state=" + QUrl::toPercentEncoding(second.queryItemValue(QStringLiteral("state"))) + "&code=ms-dummy-code");
        QTRY_COMPARE(tokens.size(), 1);
        QCOMPARE(network.requests.first().url(), QUrl(QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/token")));
        QCOMPARE(errors.size(), 1);
    }
    void brokerPersistsRotationAndNeverFallsBack()
    {
        if (qEnvironmentVariable("SMOOTHMAIL_TEST_KEYCHAIN") != QStringLiteral("1")) QSKIP("Requires disposable guest credential store opt-in.");
        CredentialStore store(nullptr, QStringLiteral("SmoothMail-oauth-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        FakeNetwork network; AuthenticationBroker broker(&store, nullptr, &network);
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("test@example.test"),
            {QStringLiteral("smtp.test"), 465, QStringLiteral("test")},
            {QStringLiteral("imap.test"), 993, QStringLiteral("test"), AuthenticationMode::OAuth2, EncryptionMode::ImplicitTls,
             QStringLiteral("google"), QStringLiteral("dummy-id")}};
        const auto oauthKey = AccountRegistrar::secretKey(account.id, QStringLiteral("imap"), AuthenticationMode::OAuth2);
        const auto passwordKey = AccountRegistrar::secretKey(account.id, QStringLiteral("imap"));
        QSignalSpy completed(this, &OAuthClientTest::credentialCompleted);
        const auto callback = [this](const QString &value, const QString &error) { emit credentialCompleted(value, error); };
        store.write(oauthKey, refreshGrant(QStringLiteral("old_refresh")), callback);
        QTRY_COMPARE(completed.size(), 1); QVERIFY(completed.takeFirst().at(1).toString().isEmpty());
        store.write(passwordKey, QStringLiteral("must-not-use-password"), callback);
        QTRY_COMPARE(completed.size(), 1); QVERIFY(completed.takeFirst().at(1).toString().isEmpty());
        broker.acquire(account, QStringLiteral("imap"), callback);
        QTRY_COMPARE(completed.size(), 1); QCOMPARE(completed.takeFirst().first().toString(), QStringLiteral("dummy_access"));
        store.read(oauthKey, callback);
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(refreshTokenFromGrant(completed.takeFirst().first().toString()), QStringLiteral("dummy_refresh"));
        broker.acquire(account, QStringLiteral("imap"), callback);
        QTRY_COMPARE(completed.size(), 1); completed.clear();
        QCOMPARE(network.requests.size(), 1); // Access token reused before expiry.
        broker.invalidate(account.id);
        network.response["refresh_token"] = QString(50000, u'x'); // Credential backend cannot persist this rotation.
        broker.acquire(account, QStringLiteral("imap"), callback);
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(1).toString().isEmpty()); QVERIFY(completed.takeFirst().first().toString().isEmpty());
        network.status = 400; network.response = {{"error", "invalid_grant"}};
        broker.acquire(account, QStringLiteral("imap"), callback);
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(1).toString().isEmpty()); QVERIFY(completed.takeFirst().first().toString().isEmpty());
        const auto posts = network.requests.size();
        store.remove(oauthKey, callback); QTRY_COMPARE(completed.size(), 1); completed.clear();
        broker.acquire(account, QStringLiteral("imap"), callback);
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(1).toString().isEmpty()); QVERIFY(completed.takeFirst().first().toString().isEmpty());
        QCOMPARE(network.requests.size(), posts); // Does not read the password key or try another mechanism.
        store.remove(passwordKey, callback); QTRY_COMPARE(completed.size(), 1);
    }
};
QTEST_GUILESS_MAIN(OAuthClientTest)
#include "tst_oauthclient.moc"
