#pragma once
#include "core/account.h"
#include "core/systemsettings.h"
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QHash>
#include <memory>
class QWidget;
class QProgressDialog;
class QUndoStack;

namespace smoothmail {
class MainWindow;
class AccountRepository;
class CredentialStore;
class AccountRegistrar;
class MailService;
class SyncBudget;
class AccountDialog;
class ComposeDialog;
class DownloadHistoryDialog;
class AuthenticationBroker;
class OAuthClient;
class SystemSettingsDialog;
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
    void refresh(const QString &accountId, const QString &folder, bool full = false, bool folderOnly = false);
    void refreshAll(bool full);
    void cancelRefresh();
    void processNext();
    void connectSyncWorker(MailService *mail);
    void compose(bool reply);
    void updateEditingState();
    QPointer<MainWindow> m_window;
    QPointer<AccountDialog> m_dialog;
    QPointer<ComposeDialog> m_compose;
    QString m_replyHandle;
    QPointer<DownloadHistoryDialog> m_history;
    QPointer<QWidget> m_editor;
    bool m_submissionWaiting = false;
    AccountRepository *m_repository;
    CredentialStore *m_credentials;
    AccountRegistrar *m_registrar;
    QList<MailService *> m_syncWorkers;
    std::shared_ptr<SyncBudget> m_syncBudget;
    QString m_saslPlugins;
    MailService *m_actions;
    AuthenticationBroker *m_authentication;
    OAuthClient *m_authorization;
    bool m_authorizingSmtp = false;
    QString m_authorizingClientId;
    QString m_reauthorizingId, m_reauthorizingProtocol;
    QPointer<QProgressDialog> m_oauthProgress;
    QList<Account> m_accounts;
    struct Request { QString accountId; QString folder; bool full = false; bool folderOnly = false; };
    QQueue<Request> m_queue;
    struct Active { Request request; MailService *mail = nullptr; quint64 serial = 0; };
    QHash<QString, Active> m_active;
    QSet<QString> m_completedSyncs;
    QHash<QString, QString> m_cacheReloadErrors;
    QSet<QString> m_deletedAccounts;
    quint64 m_requestSerial = 0;
    SystemSettings m_systemSettings;
    QString m_googleSecret;
    bool m_settingsReady = false;
    QPointer<SystemSettingsDialog> m_systemDialog;
    QSet<QString> m_addedAccounts;
    void openSystemSettings();
    QUndoStack *m_operations;
    QHash<QString, QPair<QString, QString>> m_folderHandles;
    QString folderHandle(const QString &id, const QString &path);
    void performFolder(const QString &id, const QString &operation, const QString &source, const QString &destination);
    void moveFolder(const QString &handle, const QString &parent, QChar separator);
};
} // namespace smoothmail
