#include "ui/accountdialog.h"
#include "ui/composedialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>
#include <QPlainTextEdit>

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
        const auto grant = QStringLiteral("{\"kind\":\"oauth-refresh-v1\",\"refresh_token\":\"dummy-refresh\"}");
        dialog.oauthFinished(true, grant, QStringLiteral("dummy-client"), {});
        QCOMPARE(dialog.account().smtp.oauthProvider, QStringLiteral("google"));
        QCOMPARE(dialog.account().smtp.oauthClientId, QStringLiteral("dummy-client"));
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(submitted.size(), 2);
        QCOMPARE(submitted.last().at(1).toString(), grant);
        dialog.saveFinished(QStringLiteral("test error"));
    }
    void composePreservesInputOnFailure()
    {
        ComposeData data;
        data.from = QStringLiteral("me@example.test"); data.to = QStringLiteral("to@example.test"); data.body = QStringLiteral("keep this body");
        ComposeDialog dialog(data);
        QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("composeFrom"))->isReadOnly());
        QSignalSpy submitted(&dialog, &ComposeDialog::submitRequested);
        dialog.findChild<QPushButton *>(QStringLiteral("composeSend"))->click();
        QCOMPARE(submitted.size(), 1);
        QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("composeSend"))->isEnabled());
        QVERIFY(dialog.findChild<QPlainTextEdit *>(QStringLiteral("composeBody"))->isReadOnly());
        dialog.submissionFinished(false, QStringLiteral("network unavailable"));
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("composeSend"))->isEnabled());
        QVERIFY(!dialog.findChild<QPlainTextEdit *>(QStringLiteral("composeBody"))->isReadOnly());
        QCOMPARE(dialog.draft().body, data.body);
        dialog.findChild<QPushButton *>(QStringLiteral("composeSave"))->click();
        QCOMPARE(submitted.size(), 2);
        QVERIFY(submitted.last().at(1).toBool());
        dialog.submissionFinished(true, {});
        QCOMPARE(dialog.draft().body, data.body);
    }
};
QTEST_MAIN(AccountDialogTest)
#include "tst_accountdialog.moc"
