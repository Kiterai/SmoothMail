#pragma once
#include "core/account.h"
#include <QDialog>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QLabel;
class QDialogButtonBox;
class QPushButton;
class QVBoxLayout;
namespace smoothmail {
class AccountDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit AccountDialog(QWidget *parent = nullptr);
    Account account() const;
    void setAccount(const Account &account);
    void setSecret(bool smtp, const QString &secret);
    void saveFinished(const QString &error);
    void testFinished(const QString &smtpError, const QString &imapError);
    void oauthFinished(bool smtp, const QString &grant, const QString &clientId, const QString &error);
    void setOAuthGrant(bool smtp, const QString &grant, const QString &clientId = {});
signals:
    void saveRequested(const smoothmail::Account &account, const QString &smtpPassword, const QString &imapPassword);
    void testRequested(const smoothmail::Account &account, const QString &smtpPassword, const QString &imapPassword);
    void oauthRequested(bool smtp, const QString &provider, const QString &loginHint);
    void oauthCancelled();
protected:
    void reject() override;
private:
    struct ServerFields {
        QLineEdit *host;
        QSpinBox *port;
        QLineEdit *user;
        QLineEdit *password;
        QComboBox *authentication;
        QComboBox *encryption;
        QComboBox *provider;
        QString oauthGrant;
        QString clientId;
        ServerSettings value() const;
        QString secret() const;
    };
    ServerFields createServer(const QString &name, int port);
    void setBusy(bool busy);
    void invalidateTest();
    void continueTest();
    QLineEdit *m_email;
    QLabel *m_error;
    QDialogButtonBox *m_buttons;
    QVBoxLayout *m_serverLayout = nullptr;
    QPushButton *m_test = nullptr;
    ServerFields m_smtp;
    ServerFields m_imap;
    QString m_id;
    bool m_saving = false;
    bool m_authorizing = false;
    bool m_testing = false;
    bool m_testPassed = false;
    bool m_authorizingSmtp = false;
};
} // namespace smoothmail
