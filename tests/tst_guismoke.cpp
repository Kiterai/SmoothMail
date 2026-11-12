#include "app/applicationcontroller.h"
#include "mailtestserver.h"
#include "ui/accountdialog.h"
#include "ui/composedialog.h"
#include "ui/downloadhistorydialog.h"
#include "ui/mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QSslConfiguration>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeView>
#include <QToolButton>
#include <QTimer>
#include <QtTest>

using namespace smoothmail;

// Explicit guest-only test: drives the real controller/widgets against trusted
// loopback fixtures. Normal application startup never loads fixture messages.
class GuiSmokeTest final : public QObject
{
    Q_OBJECT
    void capture(const QString &name)
    {
        QTest::qWait(150); // Allow native window/compositor painting before capture.
        const auto directory = qEnvironmentVariable("SMOOTHMAIL_GUI_OUTPUT");
        QVERIFY(!directory.isEmpty());
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(QGuiApplication::primaryScreen()->grabWindow(0).save(directory + u'/' + name + QStringLiteral(".png")));
    }
    static QAction *action(MainWindow &window, const char *name)
    {
        return window.findChild<QAction *>(QLatin1String(name));
    }
    static QModelIndex subjectIndex(QTreeView *view, const QString &subject)
    {
        const auto matches = view->model()->match(view->model()->index(0, 3), Qt::DisplayRole, subject, 1,
                                                Qt::MatchExactly | Qt::MatchRecursive);
        return matches.isEmpty() ? QModelIndex{} : matches.first().siblingAtColumn(0);
    }
private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("SMOOTHMAIL_GUI_SANDBOX_TEST") != QStringLiteral("1"))
            QSKIP("Run only in the disposable Windows Sandbox; this test saves dummy guest credentials and an attachment.");
        QCoreApplication::setQuitLockEnabled(false);
        QCoreApplication::setOrganizationName(QStringLiteral("SmoothMail GUI Sandbox"));
        QCoreApplication::setApplicationName(QStringLiteral("SmoothMail GUI Sandbox"));
        QStandardPaths::setTestModeEnabled(true);
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
    }
    void nativeControllerWorkflow()
    {
        QFile cert(QStringLiteral(":/test/tls-cert.pem")), key(QStringLiteral(":/test/tls-key.pem"));
        QVERIFY(cert.open(QIODevice::ReadOnly)); QVERIFY(key.open(QIODevice::ReadOnly));
        const QSslCertificate certificate(cert.readAll());
        const QSslKey privateKey(key.readAll(), QSsl::Rsa);
        auto trust = QSslConfiguration::defaultConfiguration();
        trust.addCaCertificate(certificate); QSslConfiguration::setDefaultConfiguration(trust);
        MailTestServer smtp(MailTestServer::Protocol::Smtp, {QStringLiteral("PLAIN")});
        MailTestServer imap(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        for (auto *server : {&smtp, &imap}) {
            server->tlsMode = MailTestServer::TlsMode::Implicit;
            server->certificate = certificate; server->privateKey = privateKey;
            QVERIFY(server->listen());
        }
        imap.provideMailbox = true; imap.provideDraftFolder = true;
        const auto subject = QStringLiteral("Sandbox GUI 日本語");
        const QByteArray headers = "From: Sender <sender@example.test>\r\nTo: gui@example.test\r\n"
            "Reply-To: reply@example.test\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n"
            "Message-ID: <gui-root@smoothmail.test>\r\nSubject: =?UTF-8?B?" + subject.toUtf8().toBase64() + "?=\r\n";
        imap.messages.insert(1, headers + "MIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=x\r\n\r\n"
            "--x\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nSandbox plain body https://example.test/\r\n"
            "--x\r\nContent-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=sandbox-note.txt\r\n"
            "Content-Transfer-Encoding: base64\r\n\r\nc2FuZGJveC1hdHRhY2htZW50\r\n--x--\r\n");
        imap.messages.insert(2, "From: reply@example.test\r\nTo: gui@example.test\r\nSubject: Sandbox reply\r\n"
            "Date: Fri, 13 Nov 2026 10:01:00 +0000\r\nMessage-ID: <gui-child@smoothmail.test>\r\n"
            "In-Reply-To: <gui-root@smoothmail.test>\r\nReferences: <gui-root@smoothmail.test>\r\n\r\nReply fixture body\r\n");
        QTemporaryDir database;
        QVERIFY(database.isValid());
        MainWindow window(false);
        window.setWindowTitle(QStringLiteral("SmoothMail - OFFLINE SANDBOX GUI FIXTURE"));
        ApplicationController controller(&window, database.filePath(QStringLiteral("gui.sqlite")),
                                          qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.screen()->availableGeometry().contains(window.frameGeometry()));
        capture(QStringLiteral("01-empty-main"));
        action(window, "addAccountAction")->trigger();
        QPointer<AccountDialog> account = window.findChild<AccountDialog *>();
        QVERIFY(account); QVERIFY(QTest::qWaitForWindowExposed(account));
        auto *buttons = account->findChild<QDialogButtonBox *>();
        QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QVERIFY(!account->findChild<QLabel *>(QStringLiteral("accountError"))->text().isEmpty());
        capture(QStringLiteral("02-account-validation"));
        account->findChild<QLineEdit *>(QStringLiteral("accountEmail"))->setText(QStringLiteral("gui@example.test"));
        for (const auto &prefix : {QStringLiteral("SMTP"), QStringLiteral("IMAP4")}) {
            account->findChild<QLineEdit *>(prefix + QStringLiteral("Host"))->setText(QStringLiteral("127.0.0.1"));
            account->findChild<QSpinBox *>(prefix + QStringLiteral("Port"))->setValue(prefix == QStringLiteral("SMTP") ? smtp.port() : imap.port());
            account->findChild<QLineEdit *>(prefix + QStringLiteral("User"))->setText(QStringLiteral("gui@example.test"));
            auto *password = account->findChild<QLineEdit *>(prefix + QStringLiteral("Password"));
            password->setText(QStringLiteral("sandbox-only-dummy")); QCOMPARE(password->echoMode(), QLineEdit::Password);
        }
        QTest::mouseClick(account->findChild<QPushButton *>(QStringLiteral("connectionTest")), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(account->findChild<QLabel *>(QStringLiteral("accountError"))->text(), QStringLiteral("接続に成功しました。"), 15000);
        QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(account.isNull(), 15000);
        auto *messages = window.findChild<QTreeView *>(QStringLiteral("messageList"));
        QTRY_COMPARE_WITH_TIMEOUT(messages->model()->rowCount(), 2, 15000);
        auto root = subjectIndex(messages, subject); QVERIFY(root.isValid());
        messages->setCurrentIndex(root);
        auto *body = window.findChild<QTextBrowser *>(QStringLiteral("messageBody"));
        QVERIFY(body->toPlainText().contains(QStringLiteral("Sandbox plain body")));
        auto *headersButton = window.findChild<QToolButton *>(QStringLiteral("headerToggle"));
        QVERIFY(headersButton); QTest::mouseClick(headersButton, Qt::LeftButton);
        capture(QStringLiteral("03-populated-main-headers"));
        auto *header = messages->header();
        const QPoint subjectHeader(header->sectionViewportPosition(3) + header->sectionSize(3) / 2, header->height() / 2);
        QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, subjectHeader);
        QCOMPARE(messages->model()->index(0, 3).data().toString(), subject);
        QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, subjectHeader);
        QCOMPARE(messages->model()->index(0, 3).data().toString(), QStringLiteral("Sandbox reply"));
        action(window, "threadAction")->setChecked(true);
        QCOMPARE(messages->model()->rowCount(), 1);
        QCOMPARE(messages->model()->rowCount(messages->model()->index(0, 0)), 1);
        messages->expandAll();
        capture(QStringLiteral("04-thread-view"));
        QSignalSpy automatic(&window, &MainWindow::refreshAllRequested);
        action(window, "autoRefreshAction")->setChecked(true);
        QTRY_VERIFY_WITH_TIMEOUT(!automatic.isEmpty(), 35000);
        action(window, "autoRefreshAction")->setChecked(false);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        messages->setCurrentIndex(subjectIndex(messages, subject));
        action(window, "replyAction")->trigger();
        QPointer<ComposeDialog> compose = window.findChild<ComposeDialog *>();
        QVERIFY(compose); QVERIFY(QTest::qWaitForWindowExposed(compose));
        QVERIFY(compose->screen()->availableGeometry().contains(compose->frameGeometry()));
        QCOMPARE(compose->draft().to, QStringLiteral("reply@example.test"));
        QCOMPARE(compose->draft().reply.inReplyTo, QByteArray("gui-root@smoothmail.test"));
        capture(QStringLiteral("05-reply-dialog"));
        compose->close(); QTRY_VERIFY(compose.isNull());
        action(window, "composeAction")->trigger();
        compose = window.findChild<ComposeDialog *>(); QVERIFY(compose);
        QVERIFY(QTest::qWaitForWindowExposed(compose));
        QVERIFY(compose->screen()->availableGeometry().contains(compose->frameGeometry()));
        compose->findChild<QLineEdit *>(QStringLiteral("composeTo"))->setText(QStringLiteral("receiver@example.test"));
        compose->findChild<QLineEdit *>(QStringLiteral("composeSubject"))->setText(QStringLiteral("Sandbox GUI send"));
        auto *editor = compose->findChild<QPlainTextEdit *>(QStringLiteral("composeBody"));
        QTest::mouseClick(editor->viewport(), Qt::LeftButton); QTest::keyClicks(editor, "undo fixture");
        QTRY_VERIFY(action(window, "undoAction")->isEnabled());
        action(window, "undoAction")->trigger(); QCOMPARE(editor->toPlainText(), QString{});
        action(window, "redoAction")->trigger(); QCOMPARE(editor->toPlainText(), QStringLiteral("undo fixture"));
        editor->insertPlainText(QStringLiteral("\n日本語"));
        capture(QStringLiteral("06-compose-undo-redo"));
        QTest::mouseClick(compose->findChild<QPushButton *>(QStringLiteral("composeSend")), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(smtp.deliveredMessages.size(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(compose.isNull(), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        action(window, "composeAction")->trigger(); compose = window.findChild<ComposeDialog *>(); QVERIFY(compose);
        QVERIFY(QTest::qWaitForWindowExposed(compose));
        compose->findChild<QLineEdit *>(QStringLiteral("composeSubject"))->setText(QStringLiteral("Sandbox GUI draft"));
        QTest::mouseClick(compose->findChild<QPushButton *>(QStringLiteral("composeSave")), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(imap.savedDrafts.size(), 1, 15000);
        QTRY_VERIFY(compose->findChild<QPushButton *>(QStringLiteral("composeSave"))->isEnabled());
        compose->close(); QTRY_VERIFY(compose.isNull());
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        messages->setCurrentIndex(subjectIndex(messages, subject));
        auto *attachments = window.findChild<QListWidget *>(QStringLiteral("messageAttachments"));
        QTRY_COMPARE(attachments->count(), 1);
        QTest::mouseClick(attachments->viewport(), Qt::LeftButton, Qt::NoModifier, attachments->visualItemRect(attachments->item(0)).center());
        action(window, "downloadHistoryAction")->trigger();
        auto *history = window.findChild<DownloadHistoryDialog *>(); QVERIFY(history);
        auto *historyView = history->findChild<QTreeView *>();
        QTRY_COMPARE_WITH_TIMEOUT(historyView->model()->rowCount(), 1, 15000);
        const auto path = historyView->model()->index(0, 2).data().toString();
        QFile downloaded(path); QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), QByteArray("sandbox-attachment")); downloaded.close();
        capture(QStringLiteral("07-download-history"));
        QTimer::singleShot(100, &window, [] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (menu) { menu->setActiveAction(menu->actions().first()); QTest::keyClick(menu, Qt::Key_Return); }
        });
        QTest::mouseClick(historyView->viewport(), Qt::RightButton, Qt::NoModifier, historyView->visualRect(historyView->model()->index(0, 0)).center());
        QTRY_VERIFY(window.selectedMessage().has_value());
        QCOMPARE(window.selectedMessage()->uid, 1);
        history->close();
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(smtp.receivedAuthentication() && imap.receivedAuthentication(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        capture(QStringLiteral("08-final-main"));
        QVERIFY(QFile::remove(path));
    }
};
QTEST_MAIN(GuiSmokeTest)
#include "tst_guismoke.moc"
