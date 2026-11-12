#pragma once
#include "accountrepository.h"
#include "credentialstore.h"

namespace smoothmail {
// Publishes configuration only after both secrets were stored successfully.
class AccountRegistrar final : public QObject
{
    Q_OBJECT
public:
    AccountRegistrar(AccountRepository *repository, CredentialStore *credentials, QObject *parent = nullptr);
    void add(Account account, const QString &smtpPassword, const QString &imapPassword);
    static QString secretKey(const QString &accountId, const QString &protocol, AuthenticationMode mode = AuthenticationMode::Plain);
signals:
    void finished(const QString &id, const QString &error);
private:
    void rollback(const QString &error);
    AccountRepository *m_repository;
    CredentialStore *m_credentials;
    Account m_pending;
    bool m_busy = false;
};
} // namespace smoothmail
