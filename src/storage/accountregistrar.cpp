#include "accountregistrar.h"
#include <QPointer>
#include <QUuid>
#include "oauth/oauthclient.h"

namespace smoothmail {
AccountRegistrar::AccountRegistrar(AccountRepository *repository, CredentialStore *credentials, QObject *parent)
    : QObject(parent), m_repository(repository), m_credentials(credentials)
{
    connect(repository, &AccountRepository::inserted, this, [this](const QString &id) {
        if (!m_busy || id != m_pending.id) return;
        m_busy = false;
        m_pending = {};
        m_backup.clear(); m_editing = false;
        emit finished(id, {});
    });
    connect(repository, &AccountRepository::failed, this, [this](const QString &operation, const QString &error) {
        if (m_busy && operation == m_pending.id) rollback(error);
    });
}

QString AccountRegistrar::secretKey(const QString &id, const QString &protocol, AuthenticationMode mode)
{
    return id + u'/' + protocol + (mode == AuthenticationMode::OAuth2 ? QStringLiteral("/oauth") : QStringLiteral("/password"));
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
    if ((account.smtp.authentication == AuthenticationMode::OAuth2 && refreshTokenFromGrant(smtpPassword).isEmpty())
        || (account.imap.authentication == AuthenticationMode::OAuth2 && refreshTokenFromGrant(imapPassword).isEmpty())) {
        emit finished({}, QStringLiteral("OAuth認可がありません。通常のパスワードを代用することはできません。"));
        return;
    }
    m_pending = account;
    m_busy = true;
    m_editing = false;
    store(account, smtpPassword, imapPassword);
}

void AccountRegistrar::edit(Account account, const QString &smtp, const QString &imap)
{
    if (m_busy) { emit finished(account.id, QStringLiteral("アカウントを保存中です。")); return; }
    const auto error = validateAccount(account);
    if (!error.isEmpty()) { emit finished(account.id, error); return; }
    if ((account.smtp.authentication == AuthenticationMode::OAuth2 && refreshTokenFromGrant(smtp).isEmpty())
        || (account.imap.authentication == AuthenticationMode::OAuth2 && refreshTokenFromGrant(imap).isEmpty())) {
        emit finished(account.id, QStringLiteral("OAuthの認可が必要です。")); return;
    }
    m_pending = account; m_busy = true; m_editing = true; m_backup.clear();
    const auto smtpKey = secretKey(account.id, QStringLiteral("smtp"), account.smtp.authentication);
    const auto imapKey = secretKey(account.id, QStringLiteral("imap"), account.imap.authentication);
    const QPointer<AccountRegistrar> guard(this);
    m_credentials->read(smtpKey, [guard, account, smtp, imap, smtpKey, imapKey](const QString &value, const QString &error) {
        if (!guard) return;
        if (!error.isEmpty()) { guard->m_busy = false; emit guard->finished(account.id, error); return; }
        guard->m_backup.insert(smtpKey, value);
        guard->m_credentials->read(imapKey, [guard, account, smtp, imap, imapKey](const QString &value, const QString &error) {
            if (!guard) return;
            if (!error.isEmpty()) { guard->m_busy = false; guard->m_backup.clear(); emit guard->finished(account.id, error); return; }
            guard->m_backup.insert(imapKey, value);
            guard->store(account, smtp, imap);
        }, true);
    }, true);
}

void AccountRegistrar::store(const Account &account, const QString &smtpPassword, const QString &imapPassword)
{
    const QPointer<AccountRegistrar> guard(this);
    m_credentials->write(secretKey(account.id, QStringLiteral("imap"), account.imap.authentication), imapPassword,
        [guard, smtpPassword](const QString &, const QString &error) {
            if (!guard) return;
            if (!error.isEmpty()) {
                guard->rollback(error);
                return;
            }
            guard->m_credentials->write(secretKey(guard->m_pending.id, QStringLiteral("smtp"), guard->m_pending.smtp.authentication), smtpPassword,
                [guard](const QString &, const QString &error) {
                    if (!guard) return;
                    if (!error.isEmpty()) guard->rollback(error);
                    else if (guard->m_editing) guard->m_repository->update(guard->m_pending);
                    else guard->m_repository->insert(guard->m_pending);
                });
        });
}

void AccountRegistrar::rollback(const QString &error)
{
    if (m_editing) { restoreNext(m_backup.keys(), error); return; }
    const QPointer<AccountRegistrar> guard(this);
    m_credentials->remove(secretKey(m_pending.id, QStringLiteral("smtp"), m_pending.smtp.authentication),
        [guard, error](const QString &, const QString &smtpError) {
            if (!guard) return;
            guard->m_credentials->remove(secretKey(guard->m_pending.id, QStringLiteral("imap"), guard->m_pending.imap.authentication),
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
void AccountRegistrar::restoreNext(QStringList keys, const QString &error)
{
    if (keys.isEmpty()) {
        const auto id = m_pending.id;
        m_busy = false; m_editing = false; m_pending = {}; m_backup.clear();
        emit finished(id, error); return;
    }
    const auto key = keys.takeFirst();
    const QPointer<AccountRegistrar> guard(this);
    const auto completion = [guard, keys, error](const QString &, const QString &restoreError) {
        if (guard) guard->restoreNext(keys, error + (restoreError.isEmpty() ? QString{} : QStringLiteral("\n秘密情報の復元に失敗しました。再設定が必要です。")));
    };
    if (m_backup.value(key).isEmpty()) m_credentials->remove(key, completion);
    else m_credentials->write(key, m_backup.value(key), completion);
}
} // namespace smoothmail
