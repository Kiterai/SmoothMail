#include "ui/accountdialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

using namespace smoothmail;
class AccountDialogTest final : public QObject
{
    Q_OBJECT
private slots:
    void defaultsAreEncryptedAndPasswordsMasked()
    {
        AccountDialog dialog;
        QCOMPARE(dialog.account().smtp.encryption, EncryptionMode::ImplicitTls);
        QCOMPARE(dialog.account().imap.encryption, EncryptionMode::ImplicitTls);
        QCOMPARE(dialog.account().smtp.port, 465);
        QCOMPARE(dialog.account().imap.port, 993);
        for (const auto *name : {"SMTPPassword", "IMAP4Password"}) {
            QCOMPARE(dialog.findChild<QLineEdit *>(QLatin1String(name))->echoMode(), QLineEdit::Password);
        }
    }
    void validatesAndPreservesError()
    {
        AccountDialog dialog;
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QSignalSpy submitted(&dialog, &AccountDialog::saveRequested);
        buttons->button(QDialogButtonBox::Ok)->click();
        QVERIFY(submitted.isEmpty());
        QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("accountError"))->text().isEmpty());
        dialog.findChild<QLineEdit *>(QStringLiteral("accountEmail"))->setText(QStringLiteral("test@example.test"));
        for (const auto &prefix : {QStringLiteral("SMTP"), QStringLiteral("IMAP4")}) {
            dialog.findChild<QLineEdit *>(prefix + QStringLiteral("Host"))->setText(QStringLiteral("mail.example.test"));
            dialog.findChild<QLineEdit *>(prefix + QStringLiteral("User"))->setText(QStringLiteral("test@example.test"));
            dialog.findChild<QLineEdit *>(prefix + QStringLiteral("Password"))->setText(QStringLiteral("dummy"));
        }
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(submitted.size(), 1);
        QVERIFY(!buttons->isEnabled());
        dialog.saveFinished(QStringLiteral("permission denied"));
        QVERIFY(buttons->isEnabled());
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("accountError"))->text(), QStringLiteral("permission denied"));
        auto *auth = dialog.findChild<QComboBox *>(QStringLiteral("SMTPAuthentication"));
        auth->setCurrentIndex(auth->findData(int(AuthenticationMode::OAuth2)));
        QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("SMTPPassword"))->text().isEmpty());
        QVERIFY(!dialog.findChild<QLineEdit *>(QStringLiteral("SMTPPassword"))->isEnabled());
    }
};
QTEST_MAIN(AccountDialogTest)
#include "tst_accountdialog.moc"
