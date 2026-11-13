#include "applicationcontroller.h"
#include "ui/mainwindow.h"
#include "ui/accountdialog.h"
#include "ui/composedialog.h"
#include "ui/downloadhistorydialog.h"
#include "ui/systemsettingsdialog.h"
#include "storage/accountregistrar.h"
#include "mail/mailservice.h"
#include "mail/authenticationbroker.h"
#include <QStatusBar>
#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QProgressDialog>
#include <QSettings>
#include <QUndoStack>
#include <QUndoCommand>
#include <QUuid>
#include <utility>

namespace smoothmail {
namespace {
class OperationCommand final : public QUndoCommand {
public:
    OperationCommand(const QString &text, std::function<void()> undo, std::function<void()> redo) : QUndoCommand(text), m_undo(std::move(undo)), m_redo(std::move(redo)) {}
    void undo() override { m_undo(); }
    void redo() override { m_redo(); }
private:
    std::function<void()> m_undo, m_redo;
};
}
ApplicationController::ApplicationController(MainWindow *window, const QString &path, const QString &plugins)
    : m_window(window), m_repository(new AccountRepository(path, this)),
      m_credentials(new CredentialStore(this)), m_registrar(new AccountRegistrar(m_repository, m_credentials, this)),
      m_actions(new MailService(plugins, this)), m_authentication(new AuthenticationBroker(m_credentials, this)),
      m_authorization(new OAuthClient(this))
{
    m_saslPlugins = plugins;
    m_operations = new QUndoStack(this); m_operations->setUndoLimit(1000);
    connect(m_operations, &QUndoStack::canUndoChanged, this, &ApplicationController::updateEditingState); connect(m_operations, &QUndoStack::canRedoChanged, this, &ApplicationController::updateEditingState);
    QSettings settings;
    m_systemSettings = {settings.value(QStringLiteral("display/pageSize"), 100).toInt(), settings.value(QStringLiteral("refresh/seconds"), 30).toInt(), settings.value(QStringLiteral("google/clientId"), qEnvironmentVariable("SMOOTHMAIL_GOOGLE_CLIENT_ID")).toString()};
    m_systemSettings.parallelUpdates = settings.value(QStringLiteral("refresh/parallelUpdates"), 4).toInt();
    if (!validSettings(m_systemSettings)) { m_systemSettings.pageSize = 100; m_systemSettings.refreshSeconds = 30; m_systemSettings.parallelUpdates = 4; }
    window->applySettings(m_systemSettings);
    const bool configured = settings.value(QStringLiteral("google/configured"), false).toBool();
    m_credentials->read(QStringLiteral("system/google-client-secret"), [this, configured](const QString &secret, const QString &error) {
        if (!error.isEmpty()) { m_window->statusBar()->showMessage(QStringLiteral("システム設定の秘密情報を読み込めません: ") + error); return; }
        m_googleSecret = configured || !secret.isEmpty() ? secret : qEnvironmentVariable("SMOOTHMAIL_GOOGLE_CLIENT_SECRET");
        if (m_googleSecret.isNull()) m_googleSecret = QStringLiteral("");
        m_authentication->setGoogleSecret(m_googleSecret); m_settingsReady = true;
    }, true);
    connect(window, &MainWindow::systemSettingsRequested, this, &ApplicationController::openSystemSettings);
    window->setPagedStorage(true);
    connect(window, &MainWindow::pageRequested, m_repository, &AccountRepository::loadPage);
    connect(m_repository, &AccountRepository::pageLoaded, window, &MainWindow::applyPage);
    connect(window, &MainWindow::messageRequested, m_repository, &AccountRepository::loadMessage);
    connect(m_repository, &AccountRepository::messageLoaded, window, &MainWindow::displayLoadedMessage);
    connect(m_repository, &AccountRepository::messageLocated, this, [this](const QString &handle, const MailRecord &identity, const QString &error) {
        if (!m_compose || handle != m_replyHandle) return;
        if (!error.isEmpty() || m_deletedAccounts.contains(identity.accountId)) {
            m_compose->sourceNavigationFailed(error.isEmpty() ? QStringLiteral("アカウントは削除されています。") : error); return;
        }
        m_window->revealMessage(identity.accountId, identity.folder, identity.uidValidity, identity.uid, identity.storageFolder);
        m_compose->hide(); // Keep the draft; Ctrl+N / Ctrl+R reopens this same dialog.
        m_window->show(); m_window->raise(); m_window->activateWindow();
    });
    connect(window, &MainWindow::messageFlagRequested, this, [this](const MailRecord &identity, bool importantFlag, bool enabled) {
        const auto handle = QUuid::createUuid().toString(QUuid::WithoutBraces); const bool before = importantFlag ? identity.important : identity.seen;
        m_operations->push(new OperationCommand(QStringLiteral("メールフラグの変更"), [this, handle, identity, importantFlag, before] { m_repository->setFlagIntent(handle, identity, importantFlag, before); }, [this, handle, identity, importantFlag, enabled] { m_repository->setFlagIntent(handle, identity, importantFlag, enabled); }));
    });
    connect(window, &MainWindow::automaticFlagRequested, m_repository, &AccountRepository::queueFlag);
    connect(window, &MainWindow::messageMoveBatchStarted, this, [this] { m_operations->beginMacro(QStringLiteral("メールの移動")); });
    connect(window, &MainWindow::messageMoveBatchFinished, m_operations, &QUndoStack::endMacro);
    connect(window, &MainWindow::messageMoveRequested, this, [this](const MailRecord &identity, const QString &destination) {
        if (identity.folder == destination) return;
        const auto handle = QUuid::createUuid().toString(QUuid::WithoutBraces), source = folderHandle(identity.accountId, identity.folder), target = folderHandle(identity.accountId, destination);
        m_operations->push(new OperationCommand(QStringLiteral("メールの移動"), [this, handle, identity, source] { m_repository->setMoveIntent(handle, identity, m_folderHandles.value(source).second); }, [this, handle, identity, target] { m_repository->setMoveIntent(handle, identity, m_folderHandles.value(target).second); }));
    });
    connect(window, &MainWindow::folderMoveRequested, this, [this](const QString &id, const QString &source, const QString &parent, QChar separator) {
        const auto handle = folderHandle(id, source), oldParent = folderHandle(id, source.contains(separator) ? source.left(source.lastIndexOf(separator)) : QString{}), target = folderHandle(id, parent);
        if (m_folderHandles.value(oldParent).second == parent) return;
        m_operations->push(new OperationCommand(QStringLiteral("フォルダの移動"), [this, handle, oldParent, separator] { moveFolder(handle, oldParent, separator); }, [this, handle, target, separator] { moveFolder(handle, target, separator); }));
    });
    connect(m_repository, &AccountRepository::localOperationFinished, this, [this](const QString &id, const QString &error) { if (!error.isEmpty()) m_cacheReloadErrors[id] = error; m_repository->loadCache(id, true); m_window->requestPage(); updateEditingState(); });
    connect(m_repository, &AccountRepository::flagsUpdated, this, [this](const MailRecord &identity, const QString &error) {
        if (error.isEmpty()) { m_repository->loadCache(identity.accountId, true); m_window->requestPage(); }
        else { m_window->statusBar()->showMessage(error); m_window->requestPage(); }
    });
    window->enableAccountManagement(true);
    window->enableHistory(true);
    connect(m_repository, &AccountRepository::loaded, this, [this](const QList<Account> &accounts) {
        QStringList existing;
        for (const auto &account : m_accounts) existing.append(account.id);
        m_accounts.clear(); for (const auto &account : accounts) if (!m_deletedAccounts.contains(account.id)) m_accounts.append(account);
        m_window->setAccounts(m_accounts);
        for (const auto &account : m_accounts) if (!existing.contains(account.id)) {
            m_repository->loadCache(account.id, true);
            if (m_addedAccounts.remove(account.id)) refresh(account.id, {});
        }
    });
    connect(m_repository, &AccountRepository::failed, this, [this](const QString &operation, const QString &error) {
        if (operation == QStringLiteral("load")) {
            m_window->enableAccountManagement(false);
            m_window->statusBar()->showMessage(QStringLiteral("アカウント設定を読み込めません: ") + error);
        }
        if (operation.startsWith(QStringLiteral("cache:"))) m_window->statusBar()->showMessage(QStringLiteral("保存済みメールを読み込めません: ") + error);
        if (operation == QStringLiteral("reorder") || operation == QStringLiteral("history")) {
            auto *notice = new QMessageBox(QMessageBox::Warning,
                operation == QStringLiteral("reorder") ? QStringLiteral("並び順を保存できません") : QStringLiteral("履歴を読み込めません"), error, QMessageBox::Ok, m_window);
            notice->setTextFormat(Qt::PlainText); notice->setAttribute(Qt::WA_DeleteOnClose); notice->open();
            if (operation == QStringLiteral("reorder")) m_repository->reload();
        }
    });
    connect(m_repository, &AccountRepository::cacheLoaded, this,
        [this](const QString &id, const QString &folder, const QList<MailFolder> &folders, const QList<MailRecord> &messages) {
            if (m_deletedAccounts.contains(id)) return;
            m_window->applySnapshot(id, folder, folders, messages, {}, m_completedSyncs.remove(id) > 0);
            if (m_window->selectedAccountId() == id) m_repository->ensureFolderLoaded(id, m_window->selectedFolderFor(id));
            const auto error = m_cacheReloadErrors.take(id);
            if (!error.isEmpty()) m_window->statusBar()->showMessage(QStringLiteral("更新に失敗しました: ") + error);
        });
    connect(m_repository, &AccountRepository::snapshotSaved, this,
        [this](const QString &id, const QString &folder, const QList<MailFolder> &folders, const QList<MailRecord> &messages) {
            m_window->setRefreshing(id, false);
            m_window->applySnapshot(id, folder, folders, messages, {});
            processNext();
        });
    connect(window, &MainWindow::addAccountRequested, this, &ApplicationController::addAccount);
    connect(window, &MainWindow::editAccountRequested, this, [this](const QString &id) {
        if (canManage()) openAccount(id);
    });
    connect(window, &MainWindow::deleteAccountRequested, this, [this](const QString &id) {
        if (!canManage()) return;
        m_deletedAccounts.insert(id); m_authentication->cancelAccount(id);
        m_operations->clear(); m_folderHandles.clear();
        if (m_active.contains(id)) {
            const auto active = m_active.value(id);
            active.mail->cancelSynchronization(id);
            // Authentication has not started network work yet. Its callback is
            // invalidated by the per-request serial, without occupying a slot.
            if (!active.mail->busy()) m_active.remove(id);
        }
        for (qsizetype i = m_queue.size(); i-- > 0;) if (m_queue.at(i).accountId == id) m_queue.removeAt(i);
        for (qsizetype i = m_accounts.size(); i-- > 0;) if (m_accounts.at(i).id == id) m_accounts.removeAt(i);
        m_window->setAccounts(m_accounts);
        m_accountMutation = true; m_repository->remove(id);
    });
    connect(m_repository, &AccountRepository::removed, this, [this](const QString &id) {
        m_authentication->invalidate(id); removeSecrets(id);
    });
    connect(m_repository, &AccountRepository::failed, this, [this](const QString &operation, const QString &) {
        if (operation.startsWith(QStringLiteral("remove:"))) {
            m_deletedAccounts.remove(operation.mid(7)); m_repository->reload();
            m_accountMutation = false;
            m_window->statusBar()->showMessage(QStringLiteral("アカウントを削除できませんでした。")); processNext();
        }
    });
    connect(window, &MainWindow::folderOperationRequested, this,
        [this](const QString &id, const QString &operation, const QString &source, const QString &destination) {
            if (m_deletedAccounts.contains(id)) return;
            if (operation == QStringLiteral("create")) {
                const auto handle = folderHandle(id, destination);
                m_operations->push(new OperationCommand(QStringLiteral("フォルダの作成"), [this, id, handle] { m_repository->cancelCreatedFolder(id, m_folderHandles.value(handle).second); }, [this, id, handle] { m_repository->restoreCreatedFolder(id, m_folderHandles.value(handle).second); }));
            } else performFolder(id, operation, source, destination);
        });
    connect(m_repository, &AccountRepository::folderQueued, this, [this](const QString &id, const QString &error) {
        if (!error.isEmpty()) m_window->cancelPendingFolderEdit();
        if (!error.isEmpty()) m_cacheReloadErrors[id] = error;
        m_repository->loadCache(id, true); m_window->requestPage();
        if (!error.isEmpty()) m_window->statusBar()->showMessage(QStringLiteral("フォルダを変更できません: ") + error);
    });
    connect(m_authorization, &OAuthClient::authorizationUrlReady, this, [this](const QUrl &url) {
        if (!QDesktopServices::openUrl(url)) {
            m_authorization->cancel();
            if (m_dialog) m_dialog->oauthFinished(m_authorizingSmtp, {}, {}, QStringLiteral("外部ブラウザを開けませんでした。"));
        }
    });
    connect(m_authorization, &OAuthClient::failed, this, [this](const QString &error) {
        if (!m_reauthorizingId.isEmpty()) {
            m_window->statusBar()->showMessage(error);
            m_reauthorizingId.clear(); m_reauthorizingProtocol.clear();
            if (m_oauthProgress) { m_oauthProgress->close(); m_oauthProgress->deleteLater(); }
        } else if (m_dialog) m_dialog->oauthFinished(m_authorizingSmtp, {}, {}, error);
    });
    connect(m_authorization, &OAuthClient::tokensReady, this, [this](const OAuthTokens &tokens) {
        if (!m_reauthorizingId.isEmpty()) {
            const auto id = m_reauthorizingId, protocol = m_reauthorizingProtocol;
            m_reauthorizingId.clear(); m_reauthorizingProtocol.clear();
            if (m_oauthProgress) { m_oauthProgress->close(); m_oauthProgress->deleteLater(); }
            const QPointer<ApplicationController> guard(this);
            m_credentials->write(AccountRegistrar::secretKey(id, protocol, AuthenticationMode::OAuth2), refreshGrant(tokens.refreshToken),
                [guard, id](const QString &, const QString &error) {
                    if (!guard) return;
                    guard->m_authentication->invalidate(id);
                    guard->m_window->statusBar()->showMessage(error.isEmpty() ? QStringLiteral("OAuth認可を更新しました。") : error);
                    if (error.isEmpty()) guard->refresh(id, guard->m_window->selectedFolderFor(id));
                });
        } else if (m_dialog) m_dialog->oauthFinished(m_authorizingSmtp, refreshGrant(tokens.refreshToken), m_authorizingClientId, {});
    });
    connect(window, &MainWindow::reauthorizeRequested, this, [this](const QString &id, const QString &protocol) {
        if (m_authorization->busy()) { m_window->statusBar()->showMessage(QStringLiteral("OAuth認可を処理中です。")); return; }
        for (const auto &account : m_accounts) if (account.id == id) {
            const auto settings = protocol == QStringLiteral("smtp") ? account.smtp : account.imap;
            if (settings.authentication != AuthenticationMode::OAuth2) return;
            m_reauthorizingId = id; m_reauthorizingProtocol = protocol;
            m_oauthProgress = new QProgressDialog(QStringLiteral("外部ブラウザでOAuth認可を完了してください。"), QStringLiteral("取消"), 0, 0, m_window);
            m_oauthProgress->setWindowTitle(QStringLiteral("OAuthの再認可"));
            m_oauthProgress->setMinimumDuration(0); m_oauthProgress->setWindowModality(Qt::WindowModal); m_oauthProgress->show();
            connect(m_oauthProgress, &QProgressDialog::canceled, m_authorization, &OAuthClient::cancel);
            m_authorization->authorize(settings.oauthProvider, settings.oauthClientId, settings.user,
                settings.oauthProvider == QStringLiteral("google") ? m_googleSecret : QString{});
            break;
        }
    });
    connect(window, &MainWindow::downloadRequested, this, [this](const MailRecord &message, const QString &partId) {
        QString name = QStringLiteral("添付ファイル");
        for (const auto &item : message.document.attachments) if (item.partId == partId && !item.fileName.isEmpty()) name = item.fileName;
        m_window->statusBar()->showMessage(name + QStringLiteral(" をダウンロード中..."));
        m_repository->saveAttachment(message, partId, QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    });
    connect(m_repository, &AccountRepository::attachmentSaved, this, [this](const DownloadEntry &entry, const QString &error) {
        m_window->statusBar()->showMessage(error.isEmpty() ? entry.path + QStringLiteral(" に保存しました") : error + (entry.path.isEmpty() ? QString{} : u'\n' + entry.path));
        if (m_history) m_repository->loadDownloadHistory();
    });
    connect(m_repository, &AccountRepository::attachmentStarted, this, [this](const QString &name) {
        m_window->statusBar()->showMessage(name + QStringLiteral(" をダウンロード中..."));
    });
    connect(window, &MainWindow::historyRequested, this, [this] {
        if (!m_history) {
            m_history = new DownloadHistoryDialog(m_window); m_history->setAttribute(Qt::WA_DeleteOnClose);
            connect(m_history, &DownloadHistoryDialog::sourceRequested, this, [this](const DownloadEntry &entry) {
                bool exists = false;
                for (const auto &account : m_accounts) if (account.id == entry.accountId) exists = true;
                if (!exists) { m_window->statusBar()->showMessage(QStringLiteral("元アカウントが見つかりません。")); return; }
                m_window->revealMessage(entry.accountId, entry.folder, entry.uidValidity, entry.uid);
                m_window->show(); m_window->raise();
            });
        }
        m_history->show(); m_history->raise(); m_repository->loadDownloadHistory();
    });
    connect(m_repository, &AccountRepository::historyLoaded, this, [this](const QList<DownloadEntry> &entries) { if (m_history) m_history->setEntries(entries); });
    connect(window, &MainWindow::composeRequested, this, &ApplicationController::compose);
    connect(window, &MainWindow::accountOrderChanged, m_repository, &AccountRepository::reorder);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *focus) {
        if (m_compose && focus && m_compose->isAncestorOf(focus)
            && (qobject_cast<QLineEdit *>(focus) || qobject_cast<QPlainTextEdit *>(focus))) m_editor = focus;
        updateEditingState();
    });
    connect(window, &MainWindow::undoRedoRequested, this, [this](bool redo) {
        auto *focus = QApplication::focusWidget();
        if (auto *line = qobject_cast<QLineEdit *>(focus); line && !line->isReadOnly()) { if (redo) line->redo(); else line->undo(); }
        else if (auto *text = qobject_cast<QPlainTextEdit *>(focus); text && !text->isReadOnly()) { if (redo) text->redo(); else text->undo(); }
        else { if (redo) m_operations->redo(); else m_operations->undo(); }
        updateEditingState();
    });
    connect(window, &MainWindow::refreshRequested, m_repository, &AccountRepository::ensureFolderLoaded);
    connect(window, &MainWindow::targetRefreshRequested, this, [this](const QString &id, const QString &folder, bool full) { refresh(id, folder, full, !folder.isEmpty()); });
    connect(m_repository, &AccountRepository::initialFetchRequired, this, [this](const QString &id, const QString &folder) { refresh(id, folder); });
    connect(window, &MainWindow::refreshAllRequested, this, [this] {
        for (const auto &account : m_accounts) refresh(account.id, m_window->selectedFolderFor(account.id));
    });
    connect(window, &MainWindow::fullRefreshRequested, this, [this] {
        for (const auto &account : m_accounts) refresh(account.id, m_window->selectedFolderFor(account.id), true);
    });
    connect(m_registrar, &AccountRegistrar::finished, this, [this](const QString &id, const QString &error) {
        if (error.isEmpty() && m_editedId.isEmpty()) m_addedAccounts.insert(id);
        if (error.isEmpty() && !m_editedId.isEmpty()) {
            m_authentication->invalidate(id);
            const auto account = m_dialog->account();
            for (const auto &protocol : {QStringLiteral("smtp"), QStringLiteral("imap")}) {
                const auto mode = protocol == QStringLiteral("smtp") ? account.smtp.authentication : account.imap.authentication;
                const auto unused = mode == AuthenticationMode::OAuth2 ? AuthenticationMode::Plain : AuthenticationMode::OAuth2;
                const QPointer<ApplicationController> guard(this);
                m_credentials->remove(AccountRegistrar::secretKey(id, protocol, unused), [guard](const QString &, const QString &cleanupError) {
                    if (guard && !cleanupError.isEmpty()) guard->m_window->statusBar()->showMessage(QStringLiteral("未使用の資格情報を削除できませんでした。OS資格情報を確認してください。"));
                });
            }
        }
        if (m_dialog) m_dialog->saveFinished(error);
        Q_UNUSED(id);
    });
    connect(m_actions, &MailService::finished, this, [this](const QString &smtp, const QString &imap) {
        if (m_dialog) m_dialog->testFinished(smtp, imap);
        processNext();
    });
    for (int i = 0; i < m_systemSettings.parallelUpdates; ++i) connectSyncWorker(new MailService(plugins, this));
    connect(m_actions, &MailService::submissionFinished, this, [this](const QString &id, bool savedAsDraft, const QString &error) {
        m_submissionWaiting = false;
        if (!error.isEmpty()) m_authentication->invalidate(id);
        if (m_compose) m_compose->submissionFinished(savedAsDraft, error);
        updateEditingState();
        if (error.isEmpty()) refresh(id, m_window->selectedFolderFor(id));
        processNext();
    });
}

void ApplicationController::connectSyncWorker(MailService *mail)
{
    m_syncWorkers.append(mail);
    connect(mail, &MailService::syncProgress, m_window, &MainWindow::showSyncProgress);
    connect(mail, &MailService::synchronized, this,
        [this](const QString &id, const QString &folder, const QList<MailFolder> &folders,
               const QList<MailRecord> &messages, const QString &error) {
            m_active.remove(id);
            if (m_deletedAccounts.contains(id)) { processNext(); return; }
            if (error.isEmpty()) {
                // The mail worker already atomically published the disk-backed
                // batches. Never send the complete mailbox through the GUI.
                m_window->setRefreshing(id, false);
                m_completedSyncs.insert(id);
                m_repository->loadCache(id, true); m_window->requestPage();
                processNext();
            } else {
                m_authentication->invalidate(id);
                m_window->setRefreshing(id, false);
                m_window->applySnapshot(id, folder, folders, messages, error);
                // Earlier flag readbacks/folder commits may already be durable.
                // Show them without clearing the synchronization error marker.
                m_cacheReloadErrors[id] = error;
                m_repository->loadCache(id, true); m_window->requestPage();
                processNext();
            }
        });
}

ApplicationController::~ApplicationController()
{
    // Stop mail workers before dependencies and the GUI callbacks disappear.
    m_repository->cancelPendingReads();
    for (auto it = m_active.cbegin(); it != m_active.cend(); ++it) it.value().mail->cancelSynchronization(it.key());
    for (auto *mail : m_syncWorkers) delete mail;
    delete m_actions;
    delete m_authorization;
    delete m_authentication;
    delete m_registrar;
    delete m_credentials;
    delete m_repository;
}

void ApplicationController::openSystemSettings()
{
    if (m_systemDialog) { m_systemDialog->raise(); return; }
    if (!m_settingsReady) { m_window->statusBar()->showMessage(QStringLiteral("OS資格情報ストアの準備が完了していません。")); return; }
    m_systemDialog = new SystemSettingsDialog(m_systemSettings, m_googleSecret, m_window); m_systemDialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_systemDialog, &SystemSettingsDialog::saveRequested, this, [this](const SystemSettings &value, const QString &secret) {
        if (!validSettings(value)) { m_systemDialog->saved(QStringLiteral("設定値が範囲外です。")); return; }
        const QPointer<SystemSettingsDialog> dialog(m_systemDialog); const auto previousSecret = m_googleSecret;
        m_credentials->write(QStringLiteral("system/google-client-secret"), secret, [this, dialog, value, secret, previousSecret](const QString &, const QString &error) {
            if (!dialog) return;
            if (!error.isEmpty()) { dialog->saved(error); return; }
            QSettings settings;
            const QStringList keys{QStringLiteral("display/pageSize"), QStringLiteral("refresh/seconds"), QStringLiteral("google/clientId"), QStringLiteral("google/configured"), QStringLiteral("refresh/parallelUpdates")};
            QMap<QString, QVariant> previous;
            for (const auto &key : keys) if (settings.contains(key)) previous.insert(key, settings.value(key));
            settings.setValue(keys[0], value.pageSize); settings.setValue(keys[1], value.refreshSeconds); settings.setValue(keys[2], value.googleClientId); settings.setValue(keys[3], true);
            settings.setValue(keys[4], value.parallelUpdates); settings.sync();
            if (settings.status() != QSettings::NoError) {
                for (const auto &key : keys) { if (previous.contains(key)) settings.setValue(key, previous.value(key)); else settings.remove(key); }
                settings.sync();
                m_credentials->write(QStringLiteral("system/google-client-secret"), previousSecret, [dialog](const QString &, const QString &rollbackError) { if (dialog) dialog->saved(rollbackError.isEmpty() ? QStringLiteral("設定ファイルを保存できませんでした。変更は適用していません。") : QStringLiteral("設定保存と秘密情報の復元に失敗しました。OS資格情報ストアを確認してください。")); }); return;
            }
            m_systemSettings = value; m_googleSecret = secret; m_authentication->setGoogleSecret(secret); m_window->applySettings(value); dialog->saved({}); processNext();
        });
    });
    m_systemDialog->open();
}
QString ApplicationController::folderHandle(const QString &id, const QString &path)
{
    const auto handle = QUuid::createUuid().toString(QUuid::WithoutBraces); m_folderHandles.insert(handle, {id, path}); return handle;
}
void ApplicationController::performFolder(const QString &id, const QString &operation, const QString &source, const QString &destination)
{
    m_repository->queueFolder(id, operation, source, destination);
    if (operation != QStringLiteral("rename")) return;
    const auto separator = m_window->folderSeparator(id, source);
    for (auto it = m_folderHandles.begin(); it != m_folderHandles.end(); ++it) if (it.value().first == id && (it.value().second == source || (!separator.isNull() && it.value().second.startsWith(source + separator)))) it.value().second = destination + it.value().second.mid(source.size());
    m_window->followFolderRename(id, source, destination);
}
void ApplicationController::moveFolder(const QString &handle, const QString &parent, QChar separator)
{
    const auto source = m_folderHandles.value(handle); const auto target = m_folderHandles.value(parent).second;
    const auto leaf = source.second.mid(source.second.lastIndexOf(separator) + 1);
    performFolder(source.first, QStringLiteral("rename"), source.second, target.isEmpty() ? leaf : target + separator + leaf);
}
void ApplicationController::addAccount()
{
    openAccount({});
}
bool ApplicationController::canManage()
{
    if (m_registrar->busy()) {
        m_window->statusBar()->showMessage(QStringLiteral("アカウント設定を保存中です。完了後に再試行してください。")); return false;
    }
    return true;
}
void ApplicationController::removeSecrets(const QString &id, int index)
{
    if (index == 4) { m_accountMutation = false; processNext(); return; }
    const auto key = AccountRegistrar::secretKey(id, index < 2 ? QStringLiteral("imap") : QStringLiteral("smtp"),
        index % 2 ? AuthenticationMode::OAuth2 : AuthenticationMode::Plain);
    const QPointer<ApplicationController> guard(this);
    m_credentials->remove(key, [guard, id, index](const QString &, const QString &error) {
        if (!guard) return;
        if (!error.isEmpty()) {
            auto *notice = new QMessageBox(QMessageBox::Warning, QStringLiteral("秘密情報の削除に失敗"),
                QStringLiteral("アカウント設定は削除しましたが、OS資格情報の一部が残っています。資格情報マネージャーで削除してください。ID: ") + id, QMessageBox::Ok, guard->m_window);
            notice->setTextFormat(Qt::PlainText); notice->setAttribute(Qt::WA_DeleteOnClose); notice->open();
        }
        guard->removeSecrets(id, index + 1);
    });
}
void ApplicationController::openAccount(const QString &id)
{
    if (m_dialog) { m_dialog->raise(); m_dialog->activateWindow(); return; }
    m_dialog = new AccountDialog(m_window);
    m_editedId = id;
    connect(m_dialog, &QObject::destroyed, this, [this] {
        m_editedId.clear(); m_accountMutation = false; processNext();
    });
    m_dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_dialog, &AccountDialog::saveRequested, this, [this](const Account &account, const QString &smtp, const QString &imap) {
        if (!m_editedId.isEmpty()) m_authentication->invalidate(account.id);
        if (m_editedId.isEmpty()) m_registrar->add(account, smtp, imap);
        else m_registrar->edit(account, smtp, imap);
    });
    if (!id.isEmpty()) {
        Account account;
        for (const auto &candidate : m_accounts) if (candidate.id == id) account = candidate;
        m_dialog->setAccount(account); m_dialog->setEnabled(false);
        const QPointer<AccountDialog> dialog(m_dialog);
        m_credentials->read(AccountRegistrar::secretKey(id, QStringLiteral("smtp"), account.smtp.authentication),
            [this, dialog, account](const QString &secret, const QString &error) {
                if (!dialog) return;
                if (error.isEmpty()) dialog->setSecret(true, secret);
                m_credentials->read(AccountRegistrar::secretKey(account.id, QStringLiteral("imap"), account.imap.authentication),
                    [dialog, error](const QString &secret, const QString &imapError) {
                        if (!dialog) return;
                        if (imapError.isEmpty()) dialog->setSecret(false, secret);
                        dialog->setEnabled(true);
                        if (!error.isEmpty() || !imapError.isEmpty()) dialog->testFinished(error, imapError);
                    });
            });
    }
    connect(m_dialog, &AccountDialog::oauthRequested, this, [this](bool smtp, const QString &provider, const QString &hint) {
        m_authorizingSmtp = smtp;
        m_authorizingClientId = provider == QStringLiteral("google") ? m_systemSettings.googleClientId : qEnvironmentVariable("SMOOTHMAIL_MICROSOFT_CLIENT_ID");
        m_authorization->authorize(provider, m_authorizingClientId, hint,
            provider == QStringLiteral("google") ? m_googleSecret : QString{});
    });
    connect(m_dialog, &AccountDialog::oauthCancelled, m_authorization, &OAuthClient::cancel);
    connect(m_dialog, &AccountDialog::testRequested, this,
        [this](const Account &account, const QString &smtp, const QString &imap) {
            if (m_actions->busy() || m_submissionWaiting) {
                m_dialog->testFinished(QStringLiteral("別の送信または接続テストを処理中です。"), {});
                return;
            }
            m_submissionWaiting = true;
            const QPointer<ApplicationController> guard(this);
            m_authentication->transient(account.smtp, smtp, [guard, account, imap](const QString &smtpToken, const QString &grant, const QString &error) {
                if (!guard) return;
                if (!error.isEmpty()) {
                    guard->m_submissionWaiting = false;
                    if (guard->m_dialog) guard->m_dialog->testFinished(error, {});
                    guard->processNext(); return;
                }
                if (!grant.isEmpty() && guard->m_dialog) guard->m_dialog->setOAuthGrant(true, grant);
                guard->m_authentication->transient(account.imap, imap, [guard, account, smtpToken](const QString &imapToken, const QString &imapGrant, const QString &imapError) {
                    if (!guard) return;
                    guard->m_submissionWaiting = false;
                    if (!imapError.isEmpty()) {
                        if (guard->m_dialog) guard->m_dialog->testFinished({}, imapError);
                        guard->processNext(); return;
                    }
                    if (!imapGrant.isEmpty() && guard->m_dialog) guard->m_dialog->setOAuthGrant(false, imapGrant);
                    guard->m_actions->test(account, smtpToken, imapToken);
                });
            });
        });
    m_dialog->open();
}

void ApplicationController::refresh(const QString &id, const QString &folder, bool full, bool folderOnly)
{
    if (m_deletedAccounts.contains(id)) return;
    const auto sameTarget = [&](const Request &request) { return request.accountId == id && request.folderOnly == folderOnly && (!folderOnly || request.folder == folder); };
    if (m_active.contains(id) && sameTarget(m_active.value(id).request) && (!full || m_active.value(id).request.full)) return;
    for (auto &request : m_queue) if (sameTarget(request)) { request.full = request.full || full; return; }
    m_queue.enqueue({id, folder, full, folderOnly});
    processNext();
}

void ApplicationController::processNext()
{
    if (m_accountMutation) return;
    while (m_active.size() < m_systemSettings.parallelUpdates && !m_queue.isEmpty()) {
        int next = -1; Account account;
        // Keep each account serial, and start eligible accounts in tree order.
        for (const auto &candidate : m_accounts) {
            if (m_active.contains(candidate.id) || m_deletedAccounts.contains(candidate.id)) continue;
            for (int i = 0; i < m_queue.size(); ++i) if (m_queue.at(i).accountId == candidate.id) { account = candidate; next = i; break; }
            if (next >= 0) break;
        }
        if (next < 0) return;
        MailService *mail = nullptr;
        for (auto *worker : m_syncWorkers) {
            bool reserved = worker->busy();
            for (const auto &active : std::as_const(m_active)) if (active.mail == worker) reserved = true;
            if (!reserved) { mail = worker; break; }
        }
        if (!mail) { mail = new MailService(m_saslPlugins, this); connectSyncWorker(mail); }
        const auto request = m_queue.takeAt(next);
        const auto serial = ++m_requestSerial;
        m_active.insert(account.id, {request, mail, serial});
        m_window->setRefreshing(account.id, true);
        const QPointer<ApplicationController> guard(this);
        m_authentication->acquire(account, QStringLiteral("imap"),
        [guard, account, request, mail, serial](const QString &secret, const QString &error) {
             if (!guard) return;
             if (!guard->m_active.contains(account.id) || guard->m_active.value(account.id).serial != serial) return;
             if (guard->m_deletedAccounts.contains(account.id)) { guard->m_active.remove(account.id); guard->processNext(); return; }
            if (!error.isEmpty()) {
                guard->m_window->setRefreshing(account.id, false);
                guard->m_window->applySnapshot(account.id, {}, {}, {}, QStringLiteral("秘密情報を読み込めません: ") + error);
                 guard->m_active.remove(account.id);
                guard->processNext();
                return;
            }
             mail->synchronize(account, secret, request.folder, guard->m_repository,
                 request.folderOnly
                     ? (request.full ? MailService::SyncMode::SelectedFolder : MailService::SyncMode::SelectedRecent)
                     : (request.full ? MailService::SyncMode::All : MailService::SyncMode::Recent));
         });
    }
}

void ApplicationController::compose(bool reply)
{
    if (m_compose) { m_compose->show(); m_compose->raise(); m_compose->activateWindow(); return; }
    Account account;
    for (const auto &candidate : m_accounts) if (candidate.id == m_window->selectedAccountId()) account = candidate;
    if (account.id.isEmpty()) return;
    ComposeData draft;
    MailRecord source;
    draft.from = account.email;
    if (reply) {
        const auto parent = m_window->selectedMessage();
        if (!parent) return;
        draft = replyDraft(account.email, parent->document);
        source = *parent; source.document = {};
    }
    m_compose = new ComposeDialog(draft, m_window, reply);
    m_compose->setAttribute(Qt::WA_DeleteOnClose);
    if (reply) {
        const auto handle = QUuid::createUuid().toString(QUuid::WithoutBraces); m_replyHandle = handle;
        m_repository->trackMessage(handle, source);
        connect(m_compose, &ComposeDialog::replySourceRequested, this, [this, handle] { m_repository->locateMessage(handle); });
        connect(m_compose, &QObject::destroyed, this, [this, handle] { m_repository->forgetMessage(handle); if (m_replyHandle == handle) m_replyHandle.clear(); });
    }
    connect(m_compose, &ComposeDialog::submitRequested, this, [this, account](const ComposeData &draft, bool saveAsDraft) {
        updateEditingState();
        if (m_actions->busy() || m_submissionWaiting || m_deletedAccounts.contains(account.id)) {
            m_compose->submissionFinished(saveAsDraft, QStringLiteral("アカウントが削除されたか、別の送信または接続テストを処理中です。"));
            return;
        }
        m_submissionWaiting = true;
        const QPointer<ApplicationController> guard(this);
        m_authentication->acquire(account, saveAsDraft ? QStringLiteral("imap") : QStringLiteral("smtp"),
            [guard, account, draft, saveAsDraft](const QString &secret, const QString &error) {
                if (!guard) return;
                if (guard->m_deletedAccounts.contains(account.id)) { guard->m_submissionWaiting = false; if (guard->m_compose) guard->m_compose->submissionFinished(saveAsDraft, QStringLiteral("アカウントが削除されました。")); return; }
                if (!error.isEmpty()) {
                    guard->m_submissionWaiting = false;
                    if (guard->m_compose) guard->m_compose->submissionFinished(saveAsDraft, error);
                    guard->processNext();
                    return;
                }
                guard->m_actions->submit(account, secret, draft, saveAsDraft);
            });
    });
    for (auto *line : m_compose->findChildren<QLineEdit *>()) connect(line, &QLineEdit::textChanged, this, &ApplicationController::updateEditingState);
    for (auto *text : m_compose->findChildren<QPlainTextEdit *>()) {
        connect(text, &QPlainTextEdit::undoAvailable, this, &ApplicationController::updateEditingState);
        connect(text, &QPlainTextEdit::redoAvailable, this, &ApplicationController::updateEditingState);
    }
    connect(m_compose, &QObject::destroyed, this, [this] { m_editor = nullptr; updateEditingState(); });
    m_compose->show();
}

void ApplicationController::updateEditingState()
{
    bool undo = m_operations->canUndo(), redo = m_operations->canRedo();
    if (const auto *line = qobject_cast<QLineEdit *>(QApplication::focusWidget()); line && !line->isReadOnly()) { undo = line->isUndoAvailable(); redo = line->isRedoAvailable(); }
    else if (const auto *text = qobject_cast<QPlainTextEdit *>(QApplication::focusWidget()); text && !text->isReadOnly()) { undo = text->document()->isUndoAvailable(); redo = text->document()->isRedoAvailable(); }
    m_window->setEditingState(undo, redo);
}
} // namespace smoothmail
