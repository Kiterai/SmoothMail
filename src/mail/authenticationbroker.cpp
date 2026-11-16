#include "authenticationbroker.h"
#include "storage/credentialstore.h"
#include "storage/accountregistrar.h"
#include "core/oauthconfiguration.h"
#include <QPointer>

namespace smoothmail {
AuthenticationBroker::AuthenticationBroker(CredentialStore *credentials, QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_credentials(credentials), m_network(network)
{
}

void AuthenticationBroker::refresh(const ServerSettings &settings, const QString &grant, TokensCompletion completion, const QString &id)
{
    const auto token = refreshTokenFromGrant(grant);
    if (token.isEmpty()) { completion({}, QStringLiteral("OAuth認可が不正です。パスワードへは切り替えません。再認可してください。")); return; }
    auto *client = new OAuthClient(this, m_network);
    if (!id.isEmpty()) { auto &requests = m_refreshes[id]; requests.removeIf([](const QPointer<OAuthClient> &request) { return request.isNull(); }); requests.append(client); }
    connect(client, &OAuthClient::failed, this, [client, completion](const QString &error) {
        client->deleteLater(); completion({}, error);
    });
    connect(client, &OAuthClient::tokensReady, this, [client, completion](const OAuthTokens &tokens) {
        client->deleteLater(); completion(tokens, {});
    });
    const auto secret = m_googleSettings
        ? oauthGoogleSecretForClient(*m_googleSettings, m_googleSecret, settings.oauthClientId)
        : (m_googleSecret.isNull() ? oauthDefaults::googleSecret() : m_googleSecret);
    client->refresh(settings.oauthProvider, settings.oauthClientId, token,
        settings.oauthProvider == QStringLiteral("google") ? secret : QString{});
}

void AuthenticationBroker::transient(const ServerSettings &settings, const QString &stored, TransientCompletion completion)
{
    if (settings.authentication != AuthenticationMode::Plain && settings.authentication != AuthenticationMode::Login
        && settings.authentication != AuthenticationMode::OAuth2) { completion({}, {}, QStringLiteral("不明な認証方式です。")); return; }
    if (settings.authentication != AuthenticationMode::OAuth2) { completion(stored, {}, {}); return; }
    refresh(settings, stored, [completion](const OAuthTokens &tokens, const QString &error) {
        completion(error.isEmpty() ? tokens.accessToken : QString{}, error.isEmpty() ? refreshGrant(tokens.refreshToken) : QString{}, error);
    });
}

void AuthenticationBroker::acquire(const Account &account, const QString &protocol, Completion completion)
{
    const auto validation = validateAccount(account);
    if (!validation.isEmpty()) { completion({}, validation); return; }
    if (protocol != QStringLiteral("imap") && protocol != QStringLiteral("smtp")) { completion({}, QStringLiteral("不明なメールプロトコルです。")); return; }
    const auto settings = protocol == QStringLiteral("imap") ? account.imap : account.smtp;
    const auto key = AccountRegistrar::secretKey(account.id, protocol, settings.authentication);
    const auto id = account.id; const auto generation = m_generations.value(id);
    if (settings.authentication == AuthenticationMode::OAuth2 && m_cache.contains(key)
        && m_cache.value(key).expiresAt > QDateTime::currentDateTimeUtc().addSecs(60)) {
        completion(m_cache.value(key).accessToken, {}); return;
    }
    const QPointer<AuthenticationBroker> guard(this);
    m_credentials->read(key, [guard, settings, key, id, generation, completion](const QString &stored, const QString &error) {
        if (!guard) return;
        if (generation < guard->m_cancelledBefore.value(id)) { completion({}, QStringLiteral("アカウントの情報取得を中断しました。")); return; }
        if (!error.isEmpty()) { completion({}, error); return; }
        if (settings.authentication != AuthenticationMode::OAuth2) { completion(stored, {}); return; }
        guard->refresh(settings, stored, [guard, key, id, generation, completion](const OAuthTokens &tokens, const QString &error) {
            if (!guard) return;
            if (guard->m_generations.value(id) != generation) { completion(error.isEmpty() ? tokens.accessToken : QString{}, error); return; }
            if (!error.isEmpty()) { guard->m_cache.remove(key); completion({}, error); return; }
            // Persist a rotated refresh token before making its access token
            // available. A keychain failure is not a success or plaintext fallback.
            guard->m_credentials->write(key, refreshGrant(tokens.refreshToken), [guard, key, id, generation, tokens, completion](const QString &, const QString &error) {
                if (!guard) return;
                if (guard->m_generations.value(id) != generation) { completion(error.isEmpty() ? tokens.accessToken : QString{}, error); return; }
                if (!error.isEmpty()) { guard->m_cache.remove(key); completion({}, error); return; }
                guard->m_cache.insert(key, tokens); completion(tokens.accessToken, {});
            });
        }, id);
    });
}

void AuthenticationBroker::invalidate(const QString &id)
{
    ++m_generations[id];
    for (const auto &key : m_cache.keys()) if (key.startsWith(id + u'/')) m_cache.remove(key);
}
void AuthenticationBroker::cancelAccount(const QString &id)
{
    invalidate(id); m_cancelledBefore[id] = m_generations.value(id);
    const auto requests = m_refreshes.take(id);
    for (const auto &request : requests) if (request) request->cancel();
}
}
