#pragma once
#include "core/account.h"
#include <QDialog>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QLabel;
class QDialogButtonBox;
class QPushButton;
namespace smoothmail {
class AccountDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit AccountDialog(QWidget *parent = nullptr);
    Account account() const;
    void saveFinished(const QString &error);
    void testFinished(const QString &smtpError, const QString &imapError);
signals:
    void saveRequested(const smoothmail::Account &account, const QString &smtpPassword, const QString &imapPassword);
    void testRequested(const smoothmail::Account &account, const QString &smtpPassword, const QString &imapPassword);
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
        ServerSettings value() const;
    };
    ServerFields createServer(const QString &name, int port);
    void setBusy(bool busy);
    QLineEdit *m_email;
    QLabel *m_error;
    QDialogButtonBox *m_buttons;
    QPushButton *m_test = nullptr;
    ServerFields m_smtp;
    ServerFields m_imap;
    QString m_id;
    bool m_saving = false;
};
} // namespace smoothmail
