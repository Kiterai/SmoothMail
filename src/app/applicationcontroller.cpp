#include "applicationcontroller.h"
#include "ui/mainwindow.h"
#include "ui/accountdialog.h"
#include "ui/composedialog.h"
#include "storage/accountregistrar.h"
#include "mail/mailservice.h"
#include <QStatusBar>

namespace smoothmail {
ApplicationController::ApplicationController(MainWindow *window, const QString &path, const QString &plugins)
    : m_window(window), m_repository(new AccountRepository(path, this)),
      m_credentials(new CredentialStore(this)), m_registrar(new AccountRegistrar(m_repository, m_credentials, this)),
      m_mail(new MailService(plugins, this))
{
    window->enableAccountManagement(true);
    connect(m_repository, &AccountRepository::loaded, this, [this](const QList<Account> &accounts) {
        QStringList existing;
        for (const auto &account : m_accounts) existing.append(account.id);
        m_accounts = accounts;
        m_window->setAccounts(accounts);
        for (const auto &account : accounts) if (!existing.contains(account.id)) {
            m_repository->loadCache(account.id);
            refresh(account.id, {});
        }
    });
    connect(m_repository, &AccountRepository::failed, this, [this](const QString &operation, const QString &error) {
        if (operation == QStringLiteral("load")) {
            m_window->enableAccountManagement(false);
            m_window->statusBar()->showMessage(QStringLiteral("アカウント設定を読み込めません: ") + error);
        }
        if (!m_current.accountId.isEmpty() && operation == QStringLiteral("snapshot:") + m_current.accountId) {
            m_window->setRefreshing(m_current.accountId, false);
            m_window->applySnapshot(m_current.accountId, {}, {}, {}, error);
            m_current = {}; processNext();
        }
        if (operation.startsWith(QStringLiteral("cache:"))) m_window->statusBar()->showMessage(QStringLiteral("保存済みメールを読み込めません: ") + error);
    });
    connect(m_repository, &AccountRepository::cacheLoaded, this,
        [this](const QString &id, const QString &folder, const QList<MailFolder> &folders, const QList<MailRecord> &messages) {
            m_window->applySnapshot(id, folder, folders, messages, {}, false);
        });
    connect(m_repository, &AccountRepository::snapshotSaved, this,
        [this](const QString &id, const QString &folder, const QList<MailFolder> &folders, const QList<MailRecord> &messages) {
            m_window->setRefreshing(id, false);
            m_window->applySnapshot(id, folder, folders, messages, {});
            m_current = {}; processNext();
        });
    connect(window, &MainWindow::addAccountRequested, this, &ApplicationController::addAccount);
    connect(window, &MainWindow::composeRequested, this, &ApplicationController::compose);
    connect(window, &MainWindow::refreshRequested, this, &ApplicationController::refresh);
    connect(window, &MainWindow::refreshAllRequested, this, [this] {
        for (const auto &account : m_accounts) refresh(account.id, m_window->selectedFolderFor(account.id));
    });
    connect(m_registrar, &AccountRegistrar::finished, this, [this](const QString &id, const QString &error) {
        if (m_dialog) m_dialog->saveFinished(error);
        Q_UNUSED(id);
    });
    connect(m_mail, &MailService::finished, this, [this](const QString &smtp, const QString &imap) {
        if (m_dialog) m_dialog->testFinished(smtp, imap);
        processNext();
    });
    connect(m_mail, &MailService::synchronized, this,
        [this](const QString &id, const QString &folder, const QList<MailFolder> &folders,
               const QList<MailRecord> &messages, const QString &error) {
            if (error.isEmpty()) {
                m_repository->saveSnapshot(id, folder, folders, messages);
            } else {
                m_window->setRefreshing(id, false);
                m_window->applySnapshot(id, folder, folders, messages, error);
                m_current = {}; processNext();
            }
        });
    connect(m_mail, &MailService::submissionFinished, this, [this](const QString &id, bool savedAsDraft, const QString &error) {
        m_submissionWaiting = false;
        if (m_compose) m_compose->submissionFinished(savedAsDraft, error);
        if (error.isEmpty()) refresh(id, m_window->selectedFolderFor(id));
        processNext();
    });
}

ApplicationController::~ApplicationController()
{
    // Stop mail workers before dependencies and the GUI callbacks disappear.
    delete m_mail;
    delete m_registrar;
    delete m_credentials;
    delete m_repository;
}

void ApplicationController::addAccount()
{
    if (m_dialog) { m_dialog->raise(); m_dialog->activateWindow(); return; }
    m_dialog = new AccountDialog(m_window);
    m_dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_dialog, &AccountDialog::saveRequested, m_registrar, &AccountRegistrar::add);
    connect(m_dialog, &AccountDialog::testRequested, this,
        [this](const Account &account, const QString &smtp, const QString &imap) {
            if (m_mail->busy() || !m_current.accountId.isEmpty() || m_submissionWaiting) {
                m_dialog->testFinished(QStringLiteral("メール更新中です。更新完了後に再試行してください。"), {});
                return;
            }
            m_mail->test(account, smtp, imap);
        });
    m_dialog->open();
}

void ApplicationController::refresh(const QString &id, const QString &folder)
{
    if (m_current.accountId == id && m_current.folder == folder) return;
    for (const auto &request : m_queue) if (request.accountId == id && request.folder == folder) return;
    m_queue.enqueue({id, folder});
    processNext();
}

void ApplicationController::processNext()
{
    if (m_mail->busy() || !m_current.accountId.isEmpty() || m_submissionWaiting || m_queue.isEmpty()) return;
    m_current = m_queue.dequeue();
    Account account;
    for (const auto &candidate : m_accounts) if (candidate.id == m_current.accountId) account = candidate;
    if (account.id.isEmpty()) { m_current = {}; processNext(); return; }
    m_window->setRefreshing(account.id, true);
    const QPointer<ApplicationController> guard(this);
    m_credentials->read(AccountRegistrar::secretKey(account.id, QStringLiteral("imap")),
        [guard, account](const QString &secret, const QString &error) {
            if (!guard) return;
            if (!error.isEmpty()) {
                guard->m_window->setRefreshing(account.id, false);
                guard->m_window->applySnapshot(account.id, {}, {}, {}, QStringLiteral("秘密情報を読み込めません: ") + error);
                guard->m_current = {};
                guard->processNext();
                return;
            }
            guard->m_mail->synchronize(account, secret, guard->m_current.folder);
        });
}

void ApplicationController::compose(bool reply)
{
    if (m_compose) { m_compose->raise(); m_compose->activateWindow(); return; }
    Account account;
    for (const auto &candidate : m_accounts) if (candidate.id == m_window->selectedAccountId()) account = candidate;
    if (account.id.isEmpty()) return;
    ComposeData draft;
    draft.from = account.email;
    if (reply) {
        const auto parent = m_window->selectedMessage();
        if (!parent) return;
        draft = replyDraft(account.email, parent->document);
    }
    m_compose = new ComposeDialog(draft, m_window);
    m_compose->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_compose, &ComposeDialog::submitRequested, this, [this, account](const ComposeData &draft, bool saveAsDraft) {
        if (m_mail->busy() || !m_current.accountId.isEmpty() || m_submissionWaiting) {
            m_compose->submissionFinished(saveAsDraft, QStringLiteral("メール更新中です。更新完了後に再試行してください。"));
            return;
        }
        m_submissionWaiting = true;
        const QPointer<ApplicationController> guard(this);
        m_credentials->read(AccountRegistrar::secretKey(account.id, saveAsDraft ? QStringLiteral("imap") : QStringLiteral("smtp")),
            [guard, account, draft, saveAsDraft](const QString &secret, const QString &error) {
                if (!guard) return;
                if (!error.isEmpty()) {
                    guard->m_submissionWaiting = false;
                    if (guard->m_compose) guard->m_compose->submissionFinished(saveAsDraft, error);
                    guard->processNext();
                    return;
                }
                guard->m_mail->submit(account, secret, draft, saveAsDraft);
            });
    });
    m_compose->show();
}
} // namespace smoothmail
