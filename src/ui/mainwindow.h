#pragma once

#include <QMainWindow>
#include "core/account.h"
#include "core/mailrecord.h"
#include "core/mailpage.h"
#include "core/systemsettings.h"
#include <QHash>
#include <QSet>
#include <optional>

class QCloseEvent;
class QSystemTrayIcon;
class QStandardItemModel;
class QTreeView;
class QLabel;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QProgressBar;
class QTimer;
class QPushButton;

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
    void clearAccountCache(const QString &id);
    void cancelPendingFolderEdit() { m_pendingCreateId.clear(); m_pendingCreateName.clear(); }
    QString selectedFolderFor(const QString &accountId) const { return m_accountId == accountId ? m_folder : QString{}; }
    QString selectedAccountId() const { return m_accountId; }
    std::optional<MailRecord> selectedMessage() const;
    void setEditingState(bool undoAvailable, bool redoAvailable);
    void enableHistory(bool enabled);
    void revealMessage(const QString &accountId, const QString &folder, qint64 uidValidity, qint64 uid, const QString &storageFolder = {});
    void setPagedStorage(bool enabled) { m_pagedStorage = enabled; }
    void requestPage(bool reset = false);
    void applyPage(const MailPage &page);
    void displayLoadedMessage(const MailRecord &message, const QString &error);
    void showSyncProgress(const QString &id, const QString &folder, qint64 done, qint64 total);
    void followFolderRename(const QString &id, const QString &source, const QString &destination);
    void applySettings(const SystemSettings &settings);
    QByteArray messageColumns() const;
    void restoreMessageColumns(const QByteArray &state);
    QChar folderSeparator(const QString &id, const QString &name) const;
signals:
    void messageColumnsChanged();
    void messageMoveBatchStarted();
    void messageMoveBatchFinished();
    void editAccountRequested(const QString &id);
    void deleteAccountRequested(const QString &id);
    void folderOperationRequested(const QString &id, const QString &operation, const QString &source, const QString &destination);
    void addAccountRequested();
    void refreshAllRequested();
    void fullRefreshRequested();
    void refreshRequested(const QString &accountId, const QString &folder);
    void targetRefreshRequested(const QString &accountId, const QString &folder, bool full);
    void composeRequested(bool reply);
    void undoRedoRequested(bool redo);
    void systemSettingsRequested();
    void accountOrderChanged(const QStringList &accountIds);
    void historyRequested();
    void downloadRequested(const smoothmail::MailRecord &message, const QString &partId);
    void reauthorizeRequested(const QString &accountId, const QString &protocol);
    void pageRequested(const smoothmail::MailQuery &query);
    void messageRequested(const smoothmail::MailRecord &identity);
    void messageFlagRequested(const smoothmail::MailRecord &identity, bool importantFlag, bool enabled);
    void automaticFlagRequested(const smoothmail::MailRecord &identity, bool importantFlag, bool enabled);
    void folderMoveRequested(const QString &id, const QString &source, const QString &parent, QChar separator);
    void messageMoveRequested(const smoothmail::MailRecord &identity, const QString &destination);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void updatePageButtons();
    bool m_pageLoading = false;
    int m_requestedPage = 1;
    QPushButton *m_previousPage = nullptr, *m_nextPage = nullptr;
    void editFolder(const QModelIndex &index, bool create);
    QHash<QString, QString> m_syncErrors;
    QString m_pendingCreateId;
    QString m_pendingCreateName;
    void createMenus();
    void createPanes();
    void createTray();
    void restoreWindow();
    void rebuildFolders();
    void rebuildMessages();
    void changeFlag(int row, bool importantFlag, bool enabled, bool automatic = false);

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
    int m_sortColumn = 6;
    Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
    qint64 m_revealUid = 0, m_revealValidity = 0;
    QString m_revealStorageFolder;
    bool m_exiting = false;
    bool m_pagedStorage = false;
    quint64 m_queryGeneration = 0;
    QString m_filterText;
    QLineEdit *m_filter = nullptr;
    QSpinBox *m_page = nullptr;
    QCheckBox *m_importantOnly = nullptr, *m_unreadOnly = nullptr;
    QProgressBar *m_filterProgress = nullptr;
    QLabel *m_pageRange = nullptr, *m_pageCount = nullptr;
    std::optional<MailRecord> m_loadedMessage;
    int m_pageSize = MailPageSize, m_edgeSelection = 0;
    int m_pageSelection = -1;
    QTimer *m_poll = nullptr;
};

} // namespace smoothmail
