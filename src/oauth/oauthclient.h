#pragma once
#include <QObject>
#include <QDateTime>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
class QNetworkAccessManager;
class QNetworkReply;

namespace smoothmail {
struct OAuthTokens {
    QString accessToken;
    QString refreshToken;
    QDateTime expiresAt;
};
QByteArray pkceChallenge(const QByteArray &verifier);
QString refreshGrant(const QString &refreshToken);
QString refreshTokenFromGrant(const QString &grant);

// Endpoints and scopes are pinned to Google/Microsoft. No configurable HTTP
// endpoint, embedded browser, certificate override or password fallback.
class OAuthClient final : public QObject
{
    Q_OBJECT
public:
    explicit OAuthClient(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    ~OAuthClient() override;
    void authorize(const QString &provider, const QString &clientId, const QString &loginHint, const QString &clientSecret = {});
    void refresh(const QString &provider, const QString &clientId, const QString &token, const QString &clientSecret = {});
    void cancel();
    bool busy() const { return m_busy; }
signals:
    void authorizationUrlReady(const QUrl &url);
    void authorizationCodeReceived();
    void tokensReady(const smoothmail::OAuthTokens &tokens);
    void failed(const QString &error);
private:
    bool configure(const QString &provider, const QString &clientId, const QString &clientSecret);
    void postToken(const QList<QPair<QString, QString>> &parameters);
    void finishError(const QString &error);
    void acceptCallback();
    QNetworkAccessManager *m_network;
    QTcpServer m_callback;
    QTimer m_deadline;
    QPointer<QNetworkReply> m_reply;
    QString m_provider, m_clientId, m_clientSecret, m_previousRefresh;
    QUrl m_tokenUrl, m_redirect;
    QByteArray m_state, m_verifier;
    bool m_busy = false;
};
}
Q_DECLARE_METATYPE(smoothmail::OAuthTokens)
