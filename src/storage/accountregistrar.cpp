#include "accountregistrar.h"
#include <QPointer>
#include <QUuid>

namespace smoothmail {
AccountRegistrar::AccountRegistrar(AccountRepository *repository, CredentialStore *credentials, QObject *parent)
    : QObject(parent), m_repository(repository), m_credentials(credentials)
{
    connect(repository, &AccountRepository::inserted, this, [this](const QString &id) {
        if (!m_busy || id != m_pending.id) return;
        m_busy = false;
        m_pending = {};
        emit finished(id, {});
    });
    connect(repository, &AccountRepository::failed, this, [this](const QString &operation, const QString &error) {
        if (m_busy && operation == m_pending.id) rollback(error);
    });
}

QString AccountRegistrar::secretKey(const QString &id, const QString &protocol)
{
    return id + u'/' + protocol + QStringLiteral("/password");
}

void AccountRegistrar::add(Account account, const QString &smtpPassword, const QString &imapPassword)
{
    if (m_busy) {
        emit finished({}, QStringLiteral("アカウントを保存中です。"));
        return;
    }
    account.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto error = validateAccount(account);
    if (!error.isEmpty()) {
        emit finished({}, error);
        return;
    }
    if (account.smtp.authentication == AuthenticationMode::OAuth2 || account.imap.authentication == AuthenticationMode::OAuth2) {
        emit finished({}, QStringLiteral("OAuthアプリ登録・認可の連携は準備中です。パスワードを代用して保存することはできません。"));
        return;
    }
    m_pending = account;
    m_busy = true;
    const QPointer<AccountRegistrar> guard(this);
    m_credentials->write(secretKey(account.id, QStringLiteral("imap")), imapPassword,
        [guard, smtpPassword](const QString &, const QString &error) {
            if (!guard) return;
            if (!error.isEmpty()) {
                guard->rollback(error);
                return;
            }
            guard->m_credentials->write(secretKey(guard->m_pending.id, QStringLiteral("smtp")), smtpPassword,
                [guard](const QString &, const QString &error) {
                    if (!guard) return;
                    if (!error.isEmpty()) guard->rollback(error);
                    else guard->m_repository->insert(guard->m_pending);
                });
        });
}

void AccountRegistrar::rollback(const QString &error)
{
    const QPointer<AccountRegistrar> guard(this);
    m_credentials->remove(secretKey(m_pending.id, QStringLiteral("smtp")),
        [guard, error](const QString &, const QString &smtpError) {
            if (!guard) return;
            guard->m_credentials->remove(secretKey(guard->m_pending.id, QStringLiteral("imap")),
                [guard, error, smtpError](const QString &, const QString &imapError) {
                    if (!guard) return;
                    auto message = error;
                    if (!smtpError.isEmpty() || !imapError.isEmpty()) {
                        message += QStringLiteral("\n一部の秘密情報を削除できませんでした。アカウントID: ") + guard->m_pending.id;
                    }
                    const auto id = guard->m_pending.id;
                    guard->m_busy = false;
                    guard->m_pending = {};
                    emit guard->finished(id, message);
                });
        });
}
} // namespace smoothmail
