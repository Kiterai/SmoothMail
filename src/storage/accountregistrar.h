#pragma once
#include "accountrepository.h"
#include "credentialstore.h"
#include <QHash>

namespace smoothmail {
// Publishes configuration only after both secrets were stored successfully.
class AccountRegistrar final : public QObject
{
    Q_OBJECT
public:
    AccountRegistrar(AccountRepository *repository, CredentialStore *credentials, QObject *parent = nullptr);
    void add(Account account, const QString &smtpPassword, const QString &imapPassword);
    void edit(Account account, const QString &smtpPassword, const QString &imapPassword);
    static QString secretKey(const QString &accountId, const QString &protocol, AuthenticationMode mode = AuthenticationMode::Plain);
signals:
    void finished(const QString &id, const QString &error);
private:
    void rollback(const QString &error);
    void store(const Account &account, const QString &smtp, const QString &imap);
    void restoreNext(QStringList keys, const QString &error);
    QHash<QString, QString> m_backup;
    bool m_editing = false;
    AccountRepository *m_repository;
    CredentialStore *m_credentials;
    Account m_pending;
    bool m_busy = false;
};
} // namespace smoothmail
