#pragma once

#include "core/account.h"
#include "core/mailrecord.h"
#include "core/mailpage.h"
#include "core/downloadentry.h"
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
    void loadMessage(const MailRecord &identity);
    void updateFlags(const MailRecord &identity, bool seen, bool important);
    void cancelPendingReads() { ++m_pageRequest; ++m_bodyRequest; }
    // Called by the mail worker only, never the GUI. Blocking calls provide
    // backpressure: at most one bounded batch is awaiting SQLite at a time.
    QString beginSync(const QString &id, const QString &folder, qint64 validity);
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

private:
    struct State;
    State *m_state = nullptr;
    QObject *m_worker = nullptr;
    QThread m_thread;
    std::atomic<quint64> m_pageRequest{0}, m_bodyRequest{0};
};
} // namespace smoothmail
