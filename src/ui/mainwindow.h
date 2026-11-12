#pragma once

#include <QMainWindow>
#include "core/account.h"
#include "core/mailrecord.h"
#include <QHash>
#include <QSet>
#include <optional>

class QCloseEvent;
class QSystemTrayIcon;
class QStandardItemModel;
class QTreeView;
class QLabel;

namespace smoothmail {

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    // Tests and environments without a tray can opt out of desktop integration.
    explicit MainWindow(bool enableSystemTray = true, QWidget *parent = nullptr);
    void enableAccountManagement(bool enabled);
    void setAccounts(const QList<Account> &accounts);
    void applySnapshot(const QString &accountId, const QString &folder, const QList<MailFolder> &folders,
                       const QList<MailRecord> &messages, const QString &error, bool remoteUpdate = true);
    void setRefreshing(const QString &accountId, bool refreshing);
    QString selectedFolderFor(const QString &accountId) const { return m_accountId == accountId ? m_folder : QString{}; }
    QString selectedAccountId() const { return m_accountId; }
    std::optional<MailRecord> selectedMessage() const;
    void setEditingState(bool undoAvailable, bool redoAvailable);
    void enableHistory(bool enabled);
    void revealMessage(const QString &accountId, const QString &folder, qint64 uidValidity, qint64 uid);
signals:
    void addAccountRequested();
    void refreshAllRequested();
    void refreshRequested(const QString &accountId, const QString &folder);
    void composeRequested(bool reply);
    void undoRedoRequested(bool redo);
    void accountOrderChanged(const QStringList &accountIds);
    void historyRequested();
    void downloadRequested(const smoothmail::MailRecord &message, const QString &partId);
    void reauthorizeRequested(const QString &accountId, const QString &protocol);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createPanes();
    void createTray();
    void restoreWindow();
    void rebuildFolders();
    void rebuildMessages();

    QSystemTrayIcon *m_tray = nullptr;
    QStandardItemModel *m_folders = nullptr;
    QStandardItemModel *m_messageModel = nullptr;
    QTreeView *m_folderView = nullptr;
    QTreeView *m_messageView = nullptr;
    QList<Account> m_accounts;
    QHash<QString, QList<MailFolder>> m_folderCache;
    QHash<QString, QHash<QString, QList<MailRecord>>> m_mailCache;
    QList<MailRecord> m_visibleMessages;
    QSet<QString> m_refreshing;
    QString m_accountId;
    QString m_folder;
    bool m_rebuilding = false;
    QLabel *m_lastUpdated = nullptr;
    QDateTime m_lastSuccess;
    qint64 m_revealUid = 0, m_revealValidity = 0;
    bool m_exiting = false;
};

} // namespace smoothmail
