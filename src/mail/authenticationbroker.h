#pragma once
#include "core/account.h"
#include "oauthclient.h"
#include <QHash>
#include <functional>

namespace smoothmail {
class CredentialStore;
class AuthenticationBroker final : public QObject
{
    Q_OBJECT
public:
    using Completion = std::function<void(const QString &secret, const QString &error)>;
    using TransientCompletion = std::function<void(const QString &secret, const QString &updatedGrant, const QString &error)>;
    AuthenticationBroker(CredentialStore *credentials, QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    void acquire(const Account &account, const QString &protocol, Completion completion);
    void transient(const ServerSettings &settings, const QString &stored, TransientCompletion completion);
    void invalidate(const QString &accountId);
private:
    using TokensCompletion = std::function<void(const OAuthTokens &, const QString &)>;
    void refresh(const ServerSettings &settings, const QString &grant, TokensCompletion completion);
    CredentialStore *m_credentials;
    QNetworkAccessManager *m_network;
    QHash<QString, OAuthTokens> m_cache;
};
}
