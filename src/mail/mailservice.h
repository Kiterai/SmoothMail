#pragma once
#include "core/account.h"
#include "core/mailrecord.h"
#include "messagecomposer.h"
#include <QObject>
#include <QThread>

namespace smoothmail {
class MailService final : public QObject
{
    Q_OBJECT
public:
    explicit MailService(QString saslPluginDirectory, QObject *parent = nullptr);
    ~MailService() override;
    void test(const Account &account, const QString &smtpSecret, const QString &imapSecret);
    void synchronize(const Account &account, const QString &imapSecret, const QString &folder = {});
    void submit(const Account &account, const QString &secret, const ComposeData &draft, bool saveAsDraft);
    bool busy() const { return m_busy; }
signals:
    void finished(const QString &smtpError, const QString &imapError);
    void synchronized(const QString &accountId, const QString &folder,
                      const QList<smoothmail::MailFolder> &folders,
                      const QList<smoothmail::MailRecord> &messages, const QString &error);
    void submissionFinished(const QString &accountId, bool savedAsDraft, const QString &error);
private:
    QObject *m_worker;
    QThread m_thread;
    QString m_pluginDirectory;
    bool m_busy = false;
};
} // namespace smoothmail
