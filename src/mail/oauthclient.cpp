#include "oauthclient.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QRegularExpression>
#include <memory>

namespace smoothmail {
namespace {
QByteArray randomUrlValue()
{
    QByteArray data;
    for (int i = 0; i < 8; ++i) {
        const auto value = QRandomGenerator::system()->generate();
        data.append(reinterpret_cast<const char *>(&value), sizeof(value));
    }
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QByteArray form(const QList<QPair<QString, QString>> &parameters)
{
    QByteArray result;
    for (const auto &parameter : parameters) {
        if (!result.isEmpty()) result += '&';
        result += QUrl::toPercentEncoding(parameter.first) + '=' + QUrl::toPercentEncoding(parameter.second);
    }
    return result;
}
QString scope(const QString &provider)
{
    return provider == QStringLiteral("google") ? QStringLiteral("https://mail.google.com/")
        : QStringLiteral("https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/SMTP.Send offline_access");
}
bool safeOpaque(const QString &value)
{
    if (value.isEmpty() || value.size() > 65536) return false;
    for (const auto c : value) if (c.unicode() < 0x21 || c.unicode() > 0x7e) return false;
    return true;
}
}

QByteArray pkceChallenge(const QByteArray &verifier)
{
    return QCryptographicHash::hash(verifier, QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QString refreshGrant(const QString &token)
{
    if (!safeOpaque(token)) return {};
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"kind", "oauth-refresh-v1"}, {"refresh_token", token}}).toJson(QJsonDocument::Compact));
}
QString refreshTokenFromGrant(const QString &grant)
{
    const auto json = QJsonDocument::fromJson(grant.toUtf8()).object();
    const auto token = json.value("refresh_token").toString();
    return json.value("kind").toString() == QStringLiteral("oauth-refresh-v1") && safeOpaque(token) ? token : QString{};
}

OAuthClient::OAuthClient(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_network(network ? network : new QNetworkAccessManager(this))
{
    qRegisterMetaType<OAuthTokens>();
    m_callback.setMaxPendingConnections(4);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] { finishError(QStringLiteral("OAuth認可がタイムアウトしました。")); });
    connect(&m_callback, &QTcpServer::newConnection, this, &OAuthClient::acceptCallback);
}
OAuthClient::~OAuthClient()
{
    if (m_reply) { disconnect(m_reply, nullptr, this, nullptr); m_reply->abort(); }
}

bool OAuthClient::configure(const QString &provider, const QString &id, const QString &secret)
{
    if (m_busy) return false;
    if ((provider != QStringLiteral("google") && provider != QStringLiteral("microsoft")) || id.trimmed().isEmpty()) {
        emit failed(QStringLiteral("OAuthプロバイダまたはクライアントIDが未設定です。アプリ登録情報を設定してください。"));
        return false;
    }
    m_busy = true; m_provider = provider; m_clientId = id; m_clientSecret = secret; m_previousRefresh.clear();
    m_tokenUrl = QUrl(provider == QStringLiteral("google") ? QStringLiteral("https://oauth2.googleapis.com/token")
        : QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/token"));
    return true;
}

void OAuthClient::authorize(const QString &provider, const QString &id, const QString &hint, const QString &secret)
{
    if (!configure(provider, id, secret)) return;
    if (!m_callback.listen(QHostAddress::LocalHost, 0)) { finishError(QStringLiteral("OAuth用loopbackポートを開けません。")); return; }
    m_state = randomUrlValue(); m_verifier = randomUrlValue();
    m_redirect = QUrl(QStringLiteral("http://%1:%2/").arg(provider == QStringLiteral("microsoft") ? QStringLiteral("localhost") : QStringLiteral("127.0.0.1")).arg(m_callback.serverPort()));
    QUrl url(provider == QStringLiteral("google") ? QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth")
        : QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/authorize"));
    QList<QPair<QString, QString>> parameters{{"client_id", id}, {"redirect_uri", m_redirect.toString()}, {"response_type", "code"},
        {"scope", scope(provider)}, {"state", QString::fromLatin1(m_state)}, {"code_challenge", QString::fromLatin1(pkceChallenge(m_verifier))},
        {"code_challenge_method", "S256"}};
    if (provider == QStringLiteral("microsoft")) parameters.emplaceBack("response_mode", "query");
    if (!hint.isEmpty()) parameters.emplaceBack("login_hint", hint);
    if (provider == QStringLiteral("google")) { parameters.emplaceBack("access_type", "offline"); parameters.emplaceBack("prompt", "consent"); }
    url.setQuery(QString::fromLatin1(form(parameters)), QUrl::StrictMode);
    m_deadline.start(5 * 60 * 1000);
    emit authorizationUrlReady(url);
}

void OAuthClient::acceptCallback()
{
    while (m_callback.hasPendingConnections()) {
        auto *socket = m_callback.nextPendingConnection();
        socket->setReadBufferSize(16385);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(5000, socket, [socket] { socket->disconnectFromHost(); });
        const auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            *buffer += socket->readAll();
            const auto respond = [socket](bool accepted) {
                socket->write(accepted ? "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\n\r\nReturn to SmoothMail. You may close this tab."
                                       : "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nCache-Control: no-store\r\n\r\nInvalid OAuth callback.");
                socket->disconnectFromHost();
            };
            if (buffer->size() > 16384 || !socket->peerAddress().isLoopback()) { respond(false); return; }
            if (!buffer->contains("\r\n\r\n")) return;
            if (!m_busy || !m_callback.isListening()) { respond(false); return; }
            const auto request = buffer->left(buffer->indexOf("\r\n")).split(' ');
            if (request.size() != 3 || request.first() != "GET" || !request.at(1).startsWith('/') || request.at(1).startsWith("//")) { respond(false); return; }
            const QUrl target = QUrl::fromEncoded(request.at(1), QUrl::StrictMode);
            if (!target.isValid() || target.path() != m_redirect.path() || target.hasFragment()) { respond(false); return; }
            QByteArray host;
            int hostCount = 0;
            for (const auto &line : buffer->split('\n')) {
                if (line.toLower().startsWith("host:")) { host = line.mid(5).trimmed(); ++hostCount; }
            }
            if (hostCount != 1 || host.toLower() != (m_redirect.host().toLatin1() + ':' + QByteArray::number(m_redirect.port())).toLower()) { respond(false); return; }
            const QUrlQuery query(target);
            const auto items = query.queryItems(QUrl::FullyDecoded);
            int states = 0, codes = 0, errors = 0;
            QString state, code;
            for (const auto &item : items) {
                if (item.first == QStringLiteral("state")) { ++states; state = item.second; }
                if (item.first == QStringLiteral("code")) { ++codes; code = item.second; }
                if (item.first == QStringLiteral("error")) ++errors;
            }
            if (states != 1 || state.toLatin1() != m_state || errors > 1 || codes > 1 || (codes && errors)) { respond(false); return; }
            if (errors == 1) { respond(true); finishError(QStringLiteral("OAuth認可が拒否または取り消されました。")); return; }
            if (codes != 1 || !safeOpaque(code)) { respond(false); return; }
            respond(true); m_callback.close(); m_deadline.stop();
            postToken({{"grant_type", "authorization_code"}, {"code", code}, {"redirect_uri", m_redirect.toString()},
                       {"code_verifier", QString::fromLatin1(m_verifier)}});
            m_state.clear(); m_verifier.clear();
        });
    }
}

void OAuthClient::refresh(const QString &provider, const QString &id, const QString &token, const QString &secret)
{
    if (!configure(provider, id, secret)) return;
    if (!safeOpaque(token)) { finishError(QStringLiteral("保存済みOAuth認可が不正です。再認可してください。")); return; }
    m_previousRefresh = token;
    QList<QPair<QString, QString>> parameters{{"grant_type", "refresh_token"}, {"refresh_token", token}};
    if (provider == QStringLiteral("microsoft")) parameters.emplaceBack("scope", scope(provider));
    postToken(parameters);
}

void OAuthClient::postToken(const QList<QPair<QString, QString>> &parameters)
{
    auto values = parameters;
    values.emplaceBack("client_id", m_clientId);
    if (!m_clientSecret.isEmpty()) values.emplaceBack("client_secret", m_clientSecret);
    QNetworkRequest request(m_tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    m_reply = m_network->post(request, form(values));
    auto *reply = m_reply.data();
    reply->setReadBufferSize(128 * 1024 + 1);
    m_deadline.start(30000);
    const auto buffer = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, buffer] {
        *buffer += reply->readAll();
        if (buffer->size() > 128 * 1024) { disconnect(reply, nullptr, this, nullptr); reply->abort(); reply->deleteLater(); m_reply = nullptr; finishError(QStringLiteral("OAuth応答がサイズ上限を超えています。")); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, buffer] {
        *buffer += reply->readAll();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error(); reply->deleteLater(); m_reply = nullptr;
        m_deadline.stop();
        if (!m_busy) return;
        const auto document = QJsonDocument::fromJson(*buffer);
        const auto json = document.object();
        if (json.value("error").toString() == QStringLiteral("invalid_grant")) { finishError(QStringLiteral("OAuth認可が失効しています。再認可してください。")); return; }
        // Report only recognized protocol codes, never raw provider text that
        // might contain authorization codes, tokens or other request data.
        const auto providerError = json.value("error").toString();
        if (error != QNetworkReply::NoError || status != 200) {
            if (providerError == QStringLiteral("invalid_client")) {
                finishError(QStringLiteral("OAuthアプリの登録情報が拒否されました (invalid_client)。クライアント種別・IDと、Google Desktop登録情報のclient_secretを確認してください。")); return;
            }
            if (providerError == QStringLiteral("unauthorized_client") || providerError == QStringLiteral("invalid_scope")
                || providerError == QStringLiteral("invalid_request")) {
                finishError(QStringLiteral("OAuthアプリまたは要求の設定に問題があります (%1)。登録種別・権限・redirect URIを確認してください。").arg(providerError)); return;
            }
        }
        if (error != QNetworkReply::NoError || status != 200 || !document.isObject() || buffer->size() > 128 * 1024) {
            finishError(QStringLiteral("OAuthトークンを取得できませんでした。TLS、ネットワーク、アプリ登録と同意設定を確認してください。")); return;
        }
        const auto access = json.value("access_token").toString();
        const auto refresh = json.value("refresh_token").toString(m_previousRefresh);
        const int lifetime = json.value("expires_in").toInt(-1);
        static const QRegularExpression bearer(QStringLiteral("\\A[A-Za-z0-9._~+/-]+=*\\z"));
        if (!safeOpaque(access) || !bearer.match(access).hasMatch() || !safeOpaque(refresh)
            || json.value("token_type").toString().compare(QStringLiteral("Bearer"), Qt::CaseInsensitive) != 0 || lifetime <= 0) {
            finishError(QStringLiteral("OAuthトークン応答が不正か、継続利用の認可がありません。再認可してください。")); return;
        }
        const auto granted = json.value("scope").toString();
        if (!granted.isEmpty()) {
            for (const auto &required : scope(m_provider).split(u' ')) {
                const auto grantedScopes = granted.split(u' ');
                const auto shortScope = required.section(u'/', -1);
                const bool permitted = grantedScopes.contains(required)
                    || (m_provider == QStringLiteral("microsoft") && grantedScopes.contains(shortScope));
                if (required != QStringLiteral("offline_access") && !permitted) {
                    finishError(QStringLiteral("IMAP/SMTPに必要なOAuth権限が許可されていません。")); return;
                }
            }
        }
        m_busy = false; m_clientSecret.clear(); m_previousRefresh.clear();
        emit tokensReady({access, refresh, QDateTime::currentDateTimeUtc().addSecs(qMin(lifetime, 7 * 24 * 3600))});
    });
}

void OAuthClient::finishError(const QString &error)
{
    if (m_reply) { auto *reply = m_reply.data(); disconnect(reply, nullptr, this, nullptr); m_reply = nullptr; reply->abort(); reply->deleteLater(); }
    m_callback.close(); m_deadline.stop(); m_busy = false;
    m_state.clear(); m_verifier.clear(); m_clientSecret.clear(); m_previousRefresh.clear();
    emit failed(error);
}
void OAuthClient::cancel()
{
    if (m_busy) finishError(QStringLiteral("OAuth認可を取り消しました。"));
}
} // namespace smoothmail
