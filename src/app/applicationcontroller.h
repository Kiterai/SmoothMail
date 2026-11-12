#pragma once
#include "core/account.h"
#include <QObject>
#include <QPointer>
#include <QQueue>
class QWidget;
class QProgressDialog;

namespace smoothmail {
class MainWindow;
class AccountRepository;
class CredentialStore;
class AccountRegistrar;
class MailService;
class AccountDialog;
class ComposeDialog;
class DownloadHistoryDialog;
class AuthenticationBroker;
class OAuthClient;
class ApplicationController final : public QObject
{
    Q_OBJECT
public:
    ApplicationController(MainWindow *window, const QString &databasePath, const QString &saslPlugins);
    ~ApplicationController() override;
private:
    void addAccount();
    void openAccount(const QString &id);
    bool canManage();
    void removeSecrets(const QString &id, int index = 0);
    bool m_accountMutation = false;
    QString m_editedId;
    QString m_reloadId;
    void refresh(const QString &accountId, const QString &folder);
    void processNext();
    void compose(bool reply);
    void updateEditingState();
    QPointer<MainWindow> m_window;
    QPointer<AccountDialog> m_dialog;
    QPointer<ComposeDialog> m_compose;
    QPointer<DownloadHistoryDialog> m_history;
    QPointer<QWidget> m_editor;
    bool m_submissionWaiting = false;
    AccountRepository *m_repository;
    CredentialStore *m_credentials;
    AccountRegistrar *m_registrar;
    MailService *m_mail;
    AuthenticationBroker *m_authentication;
    OAuthClient *m_authorization;
    bool m_authorizingSmtp = false;
    QString m_authorizingClientId;
    QString m_reauthorizingId, m_reauthorizingProtocol;
    QPointer<QProgressDialog> m_oauthProgress;
    QList<Account> m_accounts;
    struct Request { QString accountId; QString folder; };
    QQueue<Request> m_queue;
    Request m_current;
};
} // namespace smoothmail
