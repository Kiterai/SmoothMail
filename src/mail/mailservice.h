#pragma once
#include "core/account.h"
#include "core/mailrecord.h"
#include "messagecomposer.h"
#include <QObject>
#include <QThread>
#include <atomic>

namespace smoothmail {
class AccountRepository;
class MailService final : public QObject
{
    Q_OBJECT
public:
    enum class SyncMode { SelectedFolder, Recent, All, SelectedRecent };
    explicit MailService(QString saslPluginDirectory, QObject *parent = nullptr);
    ~MailService() override;
    void test(const Account &account, const QString &smtpSecret, const QString &imapSecret);
    void synchronize(const Account &account, const QString &imapSecret, const QString &folder = {}, AccountRepository *cache = nullptr,
                     SyncMode mode = SyncMode::SelectedFolder);
    void setMessageFlag(const Account &account, const QString &secret, const MailRecord &identity, bool importantFlag, bool enabled);
    void submit(const Account &account, const QString &secret, const ComposeData &draft, bool saveAsDraft);
    bool busy() const { return m_busy; }
    void cancelSynchronization(const QString &accountId);
    void changeFolder(const Account &account, const QString &secret, const QString &operation,
                      const QString &source, const QString &destination);
signals:
    void finished(const QString &smtpError, const QString &imapError);
    void synchronized(const QString &accountId, const QString &folder,
                      const QList<smoothmail::MailFolder> &folders,
                      const QList<smoothmail::MailRecord> &messages, const QString &error);
    void submissionFinished(const QString &accountId, bool savedAsDraft, const QString &error);
    void folderChanged(const QString &accountId, const QString &error);
    void syncProgress(const QString &accountId, const QString &folder, qint64 completed, qint64 total);
    void messageFlagChanged(const smoothmail::MailRecord &identity, bool importantFlag, bool enabled, const QString &error);
private:
    QObject *m_worker;
    QThread m_thread;
    QString m_pluginDirectory;
    bool m_busy = false;
    QString m_syncAccount;
    std::atomic_bool m_cancelSync{false};
};
} // namespace smoothmail
