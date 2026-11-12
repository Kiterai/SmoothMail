#pragma once

#include "core/account.h"
#include "core/mailrecord.h"
#include "core/downloadentry.h"
#include <QObject>
#include <QThread>

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
    void reorder(const QStringList &accountIds);
    void loadCache(const QString &accountId);
    void saveSnapshot(const QString &accountId, const QString &folder, const QList<MailFolder> &folders,
                      const QList<MailRecord> &messages);
    void saveAttachment(const MailRecord &message, const QString &partId, const QString &directory);
    void loadDownloadHistory();

signals:
    void loaded(const QList<smoothmail::Account> &accounts);
    void inserted(const QString &id);
    void failed(const QString &operation, const QString &error);
    void cacheLoaded(const QString &accountId, const QString &folder, const QList<smoothmail::MailFolder> &folders,
                     const QList<smoothmail::MailRecord> &messages);
    void snapshotSaved(const QString &accountId, const QString &folder, const QList<smoothmail::MailFolder> &folders,
                       const QList<smoothmail::MailRecord> &messages);
    void attachmentSaved(const smoothmail::DownloadEntry &entry, const QString &error);
    void attachmentStarted(const QString &name);
    void historyLoaded(const QList<smoothmail::DownloadEntry> &entries);

private:
    struct State;
    State *m_state = nullptr;
    QObject *m_worker = nullptr;
    QThread m_thread;
};
} // namespace smoothmail
