#pragma once
#include "core/account.h"
#include <QObject>
#include <QThread>

namespace smoothmail {
class ConnectionTester final : public QObject
{
    Q_OBJECT
public:
    explicit ConnectionTester(QString saslPluginDirectory, QObject *parent = nullptr);
    ~ConnectionTester() override;
    void test(const Account &account, const QString &smtpSecret, const QString &imapSecret);
signals:
    void finished(const QString &smtpError, const QString &imapError);
private:
    QObject *m_worker;
    QThread m_thread;
    QString m_pluginDirectory;
    bool m_busy = false;
};
} // namespace smoothmail
