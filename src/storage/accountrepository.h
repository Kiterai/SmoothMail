#pragma once

#include "core/account.h"
#include "core/mailrecord.h"
#include "core/mailpage.h"
#include "core/downloadentry.h"
#include "core/pendingflag.h"
#include "core/pendingfolder.h"
#include "core/pendingmove.h"
#include <QObject>
#include <QThread>
#include <atomic>

namespace smoothmail {

// SQLite handles are created, used and destroyed only on the private worker.
class AccountRepository final : public QObject
{
    Q_OBJECT
public:
    explicit AccountRepository(const QString &databasePath, QObject *parent = nullptr);
    ~AccountRepository() override;
    void reload();
    void insert(const Account &account);
    void update(const Account &account);
    void remove(const QString &id);
    void reorder(const QStringList &accountIds);
    void loadCache(const QString &accountId, bool metadataOnly = false);
    void saveSnapshot(const QString &accountId, const QString &folder, const QList<MailFolder> &folders,
                      const QList<MailRecord> &messages);
    void saveAttachment(const MailRecord &message, const QString &partId, const QString &directory);
    void loadDownloadHistory();
    void loadPage(const MailQuery &query);
    void ensureFolderLoaded(const QString &id, const QString &folder);
    void setFlagIntent(const QString &handle, const MailRecord &identity, bool important, bool enabled);
    void setMoveIntent(const QString &handle, const MailRecord &identity, const QString &destination);
    void cancelCreatedFolder(const QString &id, const QString &name);
    void restoreCreatedFolder(const QString &id, const QString &name);
    QString pendingMoves(const QString &id, qint64 after, qint64 through, QList<PendingMove> &moves);
    QString moveRevision(const QString &id, qint64 &revision);
    QString markMove(qint64 revision);
    QString finishMove(const PendingMove &move, qint64 validity, qint64 uid);
    QString abandonMove(qint64 revision);
    void loadMessage(const MailRecord &identity);
    void updateFlags(const MailRecord &identity, bool seen, bool important);
    void queueFlag(const MailRecord &identity, bool importantFlag, bool enabled);
    QString prepareSync(const Account &account);
    void queueFolder(const QString &id, const QString &operation, const QString &source, const QString &destination);
    QString pendingFolders(const QString &id, qint64 after, qint64 through, QList<PendingFolder> &batch);
    QString folderRevision(const QString &id, qint64 &revision);
    QString finishFolder(const PendingFolder &operation, bool preserveMessages, const QList<MailFolder> &serverFolders);
    QString pendingFlags(const QString &id, const QString &folder, qint64 after, qint64 through, QList<PendingFlag> &batch);
    QString flagRevision(qint64 &revision);
    QString acknowledgeFlag(const PendingFlag &flag, bool serverSeen, bool serverImportant, bool exists = true);
    QString recentBoundary(const QString &id, const QString &folder, qint64 validity, QDateTime &boundary);
    void cancelPendingReads() { ++m_pageRequest; ++m_bodyRequest; }
    // Called by the mail worker only, never the GUI. Blocking calls provide
    // backpressure: at most one bounded batch is awaiting SQLite at a time.
    QString beginSync(const QString &id, const QString &folder, qint64 validity, const QDateTime &boundary = {}, quint64 operation = 0);
    QString stageMessages(const QList<MailRecord> &messages);
    QString reuseMessage(qint64 uid, bool seen, bool important, bool &found);
    QString commitSync(const QList<MailFolder> &folders, qint64 expectedCount);
    void abortSync();

signals:
    void loaded(const QList<smoothmail::Account> &accounts);
    void inserted(const QString &id);
    void removed(const QString &id);
    void failed(const QString &operation, const QString &error);
    void cacheLoaded(const QString &accountId, const QString &folder, const QList<smoothmail::MailFolder> &folders,
                     const QList<smoothmail::MailRecord> &messages);
    void snapshotSaved(const QString &accountId, const QString &folder, const QList<smoothmail::MailFolder> &folders,
                       const QList<smoothmail::MailRecord> &messages);
    void attachmentSaved(const smoothmail::DownloadEntry &entry, const QString &error);
    void attachmentStarted(const QString &name);
    void historyLoaded(const QList<smoothmail::DownloadEntry> &entries);
    void pageLoaded(const smoothmail::MailPage &page);
    void messageLoaded(const smoothmail::MailRecord &message, const QString &error);
    void flagsUpdated(const smoothmail::MailRecord &identity, const QString &error);
    void folderQueued(const QString &id, const QString &error);
    void initialFetchRequired(const QString &id, const QString &folder);
    void localOperationFinished(const QString &id, const QString &error);

private:
    struct State;
    State *m_state = nullptr;
    QObject *m_worker = nullptr;
    QThread m_thread;
    std::atomic<quint64> m_pageRequest{0}, m_bodyRequest{0};
};
} // namespace smoothmail
