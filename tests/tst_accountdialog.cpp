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
#include <QScreen>
#include <QSpinBox>

using namespace smoothmail;
class AccountDialogTest final : public QObject
{
    Q_OBJECT
private slots:
    void inputDefaultsAndOAuthVisibility()
    {
        AccountDialog dialog;
        auto *email = dialog.findChild<QLineEdit *>(QStringLiteral("accountEmail"));
        auto *imap = dialog.findChild<QComboBox *>(QStringLiteral("IMAP4OAuthProvider"));
        auto *smtp = dialog.findChild<QComboBox *>(QStringLiteral("SMTPOAuthProvider"));
        QVERIFY(imap->parentWidget()->isHidden());
        for (const auto &domain : {"outlook.com", "outlook.jp", "hotmail.com", "hotmail.co.jp"}) {
            email->setText(QStringLiteral("user@") + QLatin1String(domain));
            QCOMPARE(imap->currentData().toString(), QStringLiteral("microsoft"));
            QCOMPARE(smtp->currentData().toString(), QStringLiteral("microsoft"));
            smtp->setCurrentIndex(0);
            QCOMPARE(smtp->currentData().toString(), QStringLiteral("google"));
        }
        email->setText(QStringLiteral("user@GMAIL.COM"));
        QCOMPARE(imap->currentData().toString(), QStringLiteral("google"));
        email->setText(QStringLiteral("user@unknown.test"));
        QCOMPARE(imap->currentData().toString(), QStringLiteral("google"));
        auto *auth = dialog.findChild<QComboBox *>(QStringLiteral("IMAP4Authentication"));
        auth->setCurrentIndex(auth->findData(int(AuthenticationMode::OAuth2)));
        QVERIFY(!imap->parentWidget()->isHidden());
        auth->setCurrentIndex(0);
        QVERIFY(imap->parentWidget()->isHidden());
        for (const auto &prefix : {QStringLiteral("IMAP4"), QStringLiteral("SMTP")}) {
            auto *encryption = dialog.findChild<QComboBox *>(prefix + QStringLiteral("Encryption"));
            auto *port = dialog.findChild<QSpinBox *>(prefix + QStringLiteral("Port"));
            for (const auto mode : {EncryptionMode::None, EncryptionMode::StartTls, EncryptionMode::ImplicitTls}) {
                encryption->setCurrentIndex(encryption->findData(int(mode)));
                QCOMPARE(port->value(), prefix == QStringLiteral("SMTP")
                    ? (mode == EncryptionMode::None ? 25 : mode == EncryptionMode::StartTls ? 587 : 465)
                    : (mode == EncryptionMode::StartTls ? 993 : 143));
                port->setValue(1234);
                QCOMPARE(port->value(), 1234);
            }
        }
        auto value = dialog.account(); value.imap.encryption = EncryptionMode::StartTls; value.imap.port = 8143;
        dialog.setAccount(value);
        QCOMPARE(dialog.account().imap.port, 8143);
    }
    void editPreservesIdentityAndImapComesFirst()
    {
        AccountDialog dialog;
        auto account = dialog.account(); account.id = QStringLiteral("edit-id"); account.email = QStringLiteral("edit@example.test");
        account.imap.host = QStringLiteral("imap.example.test"); account.imap.user = account.email;
        account.smtp.host = QStringLiteral("smtp.example.test"); account.smtp.user = account.email;
        dialog.setAccount(account);
        dialog.setSecret(false, QStringLiteral("dummy"));
        QCOMPARE(dialog.account().id, account.id); QCOMPARE(dialog.account().imap.host, account.imap.host);
        QCOMPARE(dialog.findChild<QLineEdit *>(QStringLiteral("IMAP4Password"))->text(), QStringLiteral("dummy"));
        dialog.show(); QCoreApplication::processEvents();
        auto *imap = dialog.findChild<QLineEdit *>(QStringLiteral("IMAP4Host"));
        auto *smtp = dialog.findChild<QLineEdit *>(QStringLiteral("SMTPHost"));
        QVERIFY(imap->mapTo(&dialog, QPoint()).y() < smtp->mapTo(&dialog, QPoint()).y());
    }
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
    void initialSizeFitsAvailableScreen()
    {
        AccountDialog dialog;
        dialog.show();
        QCoreApplication::processEvents();
        QVERIFY(dialog.height() < dialog.screen()->availableGeometry().height());
        const auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(dialog.rect().contains(QRect(buttons->mapTo(&dialog, QPoint()), buttons->size())));
        ComposeDialog compose({});
        compose.show();
        QCoreApplication::processEvents();
        QVERIFY(compose.height() < compose.screen()->availableGeometry().height());
        for (const auto *button : compose.findChildren<QPushButton *>()) {
            QVERIFY(compose.rect().contains(QRect(button->mapTo(&compose, QPoint()), button->size())));
        }
    }
    void validatesAndPreservesError()
    {
        AccountDialog dialog;
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QSignalSpy submitted(&dialog, &AccountDialog::saveRequested);
        auto *test = dialog.findChild<QPushButton *>(QStringLiteral("connectionTest"));
        QVERIFY(test->font().bold()); QCOMPARE(test->text(), QStringLiteral("接続テスト: 未"));
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        buttons->button(QDialogButtonBox::Ok)->click();
        QVERIFY(submitted.isEmpty());
        test->click();
        QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("accountError"))->text().isEmpty());
        dialog.findChild<QLineEdit *>(QStringLiteral("accountEmail"))->setText(QStringLiteral("test@example.test"));
        for (const auto &prefix : {QStringLiteral("SMTP"), QStringLiteral("IMAP4")}) {
            dialog.findChild<QLineEdit *>(prefix + QStringLiteral("Host"))->setText(QStringLiteral("mail.example.test"));
            dialog.findChild<QLineEdit *>(prefix + QStringLiteral("User"))->setText(QStringLiteral("test@example.test"));
            dialog.findChild<QLineEdit *>(prefix + QStringLiteral("Password"))->setText(QStringLiteral("dummy"));
        }
        QSignalSpy tested(&dialog, &AccountDialog::testRequested), oauth(&dialog, &AccountDialog::oauthRequested);
        test->click(); QCOMPARE(tested.size(), 1); QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        dialog.testFinished({}, {}); QCOMPARE(test->text(), QStringLiteral("接続テスト: ✓")); QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(submitted.size(), 1);
        QVERIFY(!buttons->isEnabled());
        dialog.saveFinished(QStringLiteral("permission denied"));
        QVERIFY(buttons->isEnabled());
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("accountError"))->text(), QStringLiteral("permission denied"));
        auto *auth = dialog.findChild<QComboBox *>(QStringLiteral("SMTPAuthentication"));
        auth->setCurrentIndex(auth->findData(int(AuthenticationMode::OAuth2)));
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled()); QCOMPARE(test->text(), QStringLiteral("接続テスト: 未"));
        QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("SMTPOAuthAuthorize")));
        QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("SMTPPassword"))->text().isEmpty());
        QVERIFY(!dialog.findChild<QLineEdit *>(QStringLiteral("SMTPPassword"))->isEnabled());
        const auto grant = QStringLiteral("{\"kind\":\"oauth-refresh-v1\",\"refresh_token\":\"dummy-refresh\"}");
        test->click(); QCOMPARE(oauth.size(), 1); QCOMPARE(tested.size(), 1);
        dialog.oauthFinished(true, grant, QStringLiteral("dummy-client"), {});
        QCOMPARE(tested.size(), 2); QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        dialog.testFinished({}, {});
        QCOMPARE(dialog.account().smtp.oauthProvider, QStringLiteral("google"));
        QCOMPARE(dialog.account().smtp.oauthClientId, QStringLiteral("dummy-client"));
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(submitted.size(), 2);
        QCOMPARE(submitted.last().at(1).toString(), grant);
        dialog.saveFinished(QStringLiteral("test error"));
        dialog.findChild<QLineEdit *>(QStringLiteral("SMTPHost"))->setText(QStringLiteral("changed.example.test")); QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
    }
    void oauthTestChainsProtocolsAndCancellationCannotEnableSave()
    {
        AccountDialog dialog; auto value = dialog.account(); value.email = QStringLiteral("oauth@example.test");
        for (auto *server : {&value.smtp, &value.imap}) { server->host = QStringLiteral("mail.example.test"); server->user = value.email; server->authentication = AuthenticationMode::OAuth2; server->oauthProvider = QStringLiteral("google"); }
        value.imap.user = QStringLiteral("other@example.test");
        dialog.setAccount(value);
        QSignalSpy oauth(&dialog, &AccountDialog::oauthRequested), tested(&dialog, &AccountDialog::testRequested), cancelled(&dialog, &AccountDialog::oauthCancelled);
        auto *button = dialog.findChild<QPushButton *>(QStringLiteral("connectionTest")); auto *buttons = dialog.findChild<QDialogButtonBox *>();
        button->click(); QCOMPARE(oauth.size(), 1); QVERIFY(oauth.first().first().toBool());
        dialog.oauthFinished(true, QStringLiteral("dummy-grant"), QStringLiteral("dummy-client"), {}); QCOMPARE(oauth.size(), 2); QVERIFY(!oauth.last().first().toBool()); QVERIFY(tested.isEmpty());
        dialog.oauthFinished(false, {}, {}, QStringLiteral("cancelled")); QVERIFY(button->isEnabled()); QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        button->click(); QCOMPARE(oauth.size(), 3); buttons->button(QDialogButtonBox::Cancel)->click(); QCOMPARE(cancelled.size(), 1);
        dialog.oauthFinished(false, QStringLiteral("late-grant"), QStringLiteral("dummy-client"), {}); dialog.testFinished({}, {}); QVERIFY(tested.isEmpty()); QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
    }
    void differentProvidersDoNotShareAuthorization()
    {
        AccountDialog dialog; auto value = dialog.account(); value.email = QStringLiteral("oauth@example.test");
        for (auto *server : {&value.smtp, &value.imap}) {
            server->host = QStringLiteral("mail.example.test"); server->user = value.email;
            server->authentication = AuthenticationMode::OAuth2; server->oauthProvider = QStringLiteral("microsoft");
        }
        value.imap.oauthProvider = QStringLiteral("google");
        dialog.setAccount(value);
        QSignalSpy oauth(&dialog, &AccountDialog::oauthRequested), tested(&dialog, &AccountDialog::testRequested);
        dialog.findChild<QPushButton *>(QStringLiteral("connectionTest"))->click();
        dialog.oauthFinished(true, QStringLiteral("dummy-grant"), QStringLiteral("dummy-client"), {});
        QCOMPARE(oauth.size(), 2); QVERIFY(!oauth.last().first().toBool()); QVERIFY(tested.isEmpty());
        dialog.oauthFinished(false, {}, {}, QStringLiteral("Token exchange failed"));
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("connectionTest"))->isEnabled());
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("accountError"))->text(), QStringLiteral("Token exchange failed"));
    }
    void sameIdentityUsesOneAuthorization_data()
    {
        QTest::addColumn<QString>("provider");
        QTest::newRow("microsoft") << QStringLiteral("microsoft");
        QTest::newRow("google") << QStringLiteral("google");
    }
    void sameIdentityUsesOneAuthorization()
    {
        QFETCH(QString, provider);
        AccountDialog dialog; auto value = dialog.account(); value.email = QStringLiteral("oauth@example.test");
        for (auto *server : {&value.smtp, &value.imap}) {
            server->host = QStringLiteral("mail.example.test"); server->user = value.email;
            server->authentication = AuthenticationMode::OAuth2; server->oauthProvider = provider;
        }
        dialog.setAccount(value);
        QSignalSpy oauth(&dialog, &AccountDialog::oauthRequested), tested(&dialog, &AccountDialog::testRequested);
        auto *button = dialog.findChild<QPushButton *>(QStringLiteral("connectionTest"));
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        button->click(); QCOMPARE(oauth.size(), 1);
        dialog.oauthCodeReceived();
        QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("accountError"))->text().contains(QStringLiteral("トークンを取得中")));
        dialog.oauthFinished(true, QStringLiteral("dummy-grant"), QStringLiteral("dummy-client"), {});
        QCOMPARE(oauth.size(), 1); QCOMPARE(tested.size(), 1);
        QCOMPARE(tested.first().at(1).toString(), QStringLiteral("dummy-grant"));
        QCOMPARE(tested.first().at(2).toString(), QStringLiteral("dummy-grant"));
        QCOMPARE(dialog.account().imap.oauthClientId, QStringLiteral("dummy-client"));
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        dialog.testFinished(QStringLiteral("SMTP denied"), {});
        QVERIFY(button->isEnabled()); QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("accountError"))->text().contains(QStringLiteral("SMTP denied")));
        button->click(); QCOMPARE(oauth.size(), 2);
        dialog.oauthFinished(true, QStringLiteral("new-grant"), QStringLiteral("dummy-client"), {});
        QCOMPARE(tested.size(), 2);
        dialog.testFinished({}, {});
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
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
