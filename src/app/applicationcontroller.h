#pragma once
#include "core/account.h"
#include <QObject>
#include <QPointer>
#include <QQueue>

namespace smoothmail {
class MainWindow;
class AccountRepository;
class CredentialStore;
class AccountRegistrar;
class MailService;
class AccountDialog;
class ComposeDialog;
class ApplicationController final : public QObject
{
    Q_OBJECT
public:
    ApplicationController(MainWindow *window, const QString &databasePath, const QString &saslPlugins);
    ~ApplicationController() override;
private:
    void addAccount();
    void refresh(const QString &accountId, const QString &folder);
    void processNext();
    void compose(bool reply);
    QPointer<MainWindow> m_window;
    QPointer<AccountDialog> m_dialog;
    QPointer<ComposeDialog> m_compose;
    bool m_submissionWaiting = false;
    AccountRepository *m_repository;
    CredentialStore *m_credentials;
    AccountRegistrar *m_registrar;
    MailService *m_mail;
    QList<Account> m_accounts;
    struct Request { QString accountId; QString folder; };
    QQueue<Request> m_queue;
    Request m_current;
};
} // namespace smoothmail
