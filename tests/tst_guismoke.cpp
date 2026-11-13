#include "app/applicationcontroller.h"
#include "mailtestserver.h"
#include "ui/accountdialog.h"
#include "ui/composedialog.h"
#include "ui/downloadhistorydialog.h"
#include "ui/mainwindow.h"
#include "storage/accountregistrar.h"
#include "mail/mimeparser.h"

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
#include <QUuid>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QProgressBar>
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
    static void contextAction(QTreeView *view, const QModelIndex &index, const QString &text)
    {
        QTimer::singleShot(100, view, [text] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto *action : menu->actions()) if (action->text() == text) {
                menu->setActiveAction(action); QTest::keyClick(menu, Qt::Key_Return); return;
            }
            menu->close();
        });
        const auto position = view->visualRect(index).center();
        // QTest's synthetic mouse events do not ask Windows to generate the
        // separate WM_CONTEXTMENU event; send that Qt event explicitly.
        QContextMenuEvent event(QContextMenuEvent::Mouse, position, view->viewport()->mapToGlobal(position));
        QApplication::sendEvent(view->viewport(), &event);
    }
    static QModelIndex folderIndex(QTreeView *view, const QString &name)
    {
        const auto matches = view->model()->match(view->model()->index(0, 0), Qt::UserRole + 1, name, 1, Qt::MatchExactly | Qt::MatchRecursive);
        return matches.value(0);
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
        QTRY_VERIFY(body->toPlainText().contains(QStringLiteral("Sandbox plain body")));
        auto *headersButton = window.findChild<QToolButton *>(QStringLiteral("headerToggle"));
        QVERIFY(headersButton); QTest::mouseClick(headersButton, Qt::LeftButton);
        capture(QStringLiteral("03-populated-main-headers"));
        auto *header = messages->header();
        const QPoint subjectHeader(header->sectionViewportPosition(3) + header->sectionSize(3) / 2, header->height() / 2);
        QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, subjectHeader);
        QTRY_COMPARE(messages->model()->index(0, 3).data().toString(), subject);
        QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, subjectHeader);
        QTRY_COMPARE(messages->model()->index(0, 3).data().toString(), QStringLiteral("Sandbox reply"));
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
        QTRY_VERIFY(action(window, "replyAction")->isEnabled());
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
        auto *folders = window.findChild<QTreeView *>(QStringLiteral("folderTree"));
        const auto id = folders->model()->index(0, 0).data(Qt::UserRole).toString();
        contextAction(folders, folders->model()->index(0, 0), QStringLiteral("アカウントの編集"));
        QTRY_VERIFY(window.findChild<AccountDialog *>());
        account = window.findChild<AccountDialog *>();
        QTRY_VERIFY(account->isEnabled());
        QCOMPARE(account->account().id, id);
        QCOMPARE(account->findChild<QLineEdit *>(QStringLiteral("IMAP4Password"))->text(), QStringLiteral("sandbox-only-dummy"));
        account->findChild<QLineEdit *>(QStringLiteral("accountEmail"))->setText(QStringLiteral("edited@example.test"));
        capture(QStringLiteral("09-account-edit"));
        QTest::mouseClick(account->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(account.isNull(), 15000);
        QTRY_VERIFY(folders->model()->index(0, 0).data().toString().startsWith(QStringLiteral("edited@example.test")));
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        contextAction(folders, folders->model()->index(0, 0), QStringLiteral("新規フォルダ"));
        QTRY_VERIFY_WITH_TIMEOUT(folders->findChild<QLineEdit *>(), 15000);
        auto *inlineEditor = folders->findChild<QLineEdit *>(); QVERIFY(inlineEditor);
        inlineEditor->setText(QStringLiteral("Parent")); QTest::keyClick(inlineEditor, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(imap.extraFolders.contains("Parent"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(folderIndex(folders, QStringLiteral("Parent")).isValid(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        contextAction(folders, folderIndex(folders, QStringLiteral("Parent")), QStringLiteral("新規フォルダ"));
        QTRY_VERIFY_WITH_TIMEOUT(folders->findChild<QLineEdit *>(), 15000);
        inlineEditor = folders->findChild<QLineEdit *>(); QVERIFY(inlineEditor);
        inlineEditor->setText(QStringLiteral("Child")); QTest::keyClick(inlineEditor, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(imap.extraFolders.contains("Parent/Child"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(folderIndex(folders, QStringLiteral("Parent/Child")).isValid(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        contextAction(folders, folderIndex(folders, QStringLiteral("Parent/Child")), QStringLiteral("名前の変更"));
        inlineEditor = folders->findChild<QLineEdit *>(); QVERIFY(inlineEditor);
        inlineEditor->setText(QStringLiteral("Renamed")); QTest::keyClick(inlineEditor, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(imap.extraFolders.contains("Parent/Renamed"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(folderIndex(folders, QStringLiteral("Parent/Renamed")).isValid(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        capture(QStringLiteral("10-folder-edit"));
        // The warning defaults to Cancel; cancellation must issue no DELETE.
        QTimer::singleShot(200, &window, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->reject(); });
        contextAction(folders, folderIndex(folders, QStringLiteral("Parent/Renamed")), QStringLiteral("削除"));
        QVERIFY(imap.extraFolders.contains("Parent/Renamed"));
        QTimer::singleShot(200, &window, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->done(QMessageBox::Yes); });
        contextAction(folders, folderIndex(folders, QStringLiteral("Parent/Renamed")), QStringLiteral("削除"));
        QTRY_VERIFY_WITH_TIMEOUT(!imap.extraFolders.contains("Parent/Renamed"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        QTimer::singleShot(200, &window, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->done(QMessageBox::Yes); });
        contextAction(folders, folders->model()->index(0, 0), QStringLiteral("アカウントの削除"));
        QTRY_COMPARE_WITH_TIMEOUT(folders->model()->rowCount(), 0, 10000);
        QCOMPARE(imap.messages.size(), 2); // Account deletion never deletes server mail.
    }
    void paginatesAndFiltersLargeMailboxWithoutBlockingGui()
    {
        constexpr int count = 10034;
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}); server.provideMailbox = true;
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("large@example.test"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), 0, QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        QList<MailRecord> records;
        for (int i = 1; i <= count; ++i) {
            const auto raw = "From: Large <fixture@example.test>\r\nSubject: Large " + QByteArray::number(i).rightJustified(5, '0')
                + "\r\nMessage-ID: <large-" + QByteArray::number(i) + "@example.test>\r\n\r\n" + (i == count ? "unique needle body" : "ordinary body");
            server.messages.insert(i, raw);
            auto &flags = server.messageFlags[i]; if (i % 3) flags.append("\\Seen"); if (!(i % 2)) flags.append("\\Flagged");
            const auto parsed = MimeParser::parse(raw); QVERIFY(parsed.ok());
            records.append({account.id, QStringLiteral("INBOX"), 10, i, bool(i % 3), !(i % 2), parsed.message});
        }
        QVERIFY(server.listen()); account.imap.port = server.port();
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath(QStringLiteral("large.sqlite"));
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved);
            repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
            repository.saveSnapshot(account.id, QStringLiteral("INBOX"), {{QStringLiteral("INBOX"), u'/', {}, count / 3}}, records);
            QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 30000);
        }
        records.clear();
        CredentialStore credentials;
        for (const auto &protocol : {QStringLiteral("imap"), QStringLiteral("smtp")}) {
            bool done = false; QString error;
            credentials.write(AccountRegistrar::secretKey(account.id, protocol), QStringLiteral("sandbox-only-dummy"), [&](const QString &, const QString &value) { error = value; done = true; });
            QTRY_VERIFY(done); QVERIFY(error.isEmpty());
        }
        {
            MainWindow window(false); ApplicationController controller(&window, path, qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
            int ticks = 0; QTimer heartbeat; heartbeat.setInterval(10); connect(&heartbeat, &QTimer::timeout, &window, [&] { ++ticks; }); heartbeat.start();
            window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto *view = window.findChild<QTreeView *>(QStringLiteral("messageList"));
            auto *page = window.findChild<QSpinBox *>(QStringLiteral("mailPage"));
            auto *range = window.findChild<QLabel *>(QStringLiteral("pageRange"));
            QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), 100, 30000);
            QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 240000);
            QVERIFY(ticks > 5); QCOMPARE(server.fullFetchCount, 0); // Cache restoration did not load all bodies.
            auto *folders = window.findChild<QTreeView *>(QStringLiteral("folderTree"));
            QTRY_VERIFY(folders->model()->index(0, 0).data().toString().endsWith(QStringLiteral("(3344)")));
            QCOMPARE(page->maximum(), 101); QVERIFY(page->isEnabled());
            QTest::mouseClick(page, Qt::LeftButton); QTest::keyClick(page, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(page, "101"); QTest::keyClick(page, Qt::Key_Return);
            QTRY_COMPARE(view->model()->rowCount(), 34); QCOMPARE(range->text(), QStringLiteral("10001 - 10034 / 10034件"));
            capture(QStringLiteral("11-large-mailbox-last-page"));
            auto *filter = window.findChild<QLineEdit *>(QStringLiteral("mailFilter"));
            auto *search = window.findChild<QPushButton *>(QStringLiteral("mailSearch"));
            filter->setText(QStringLiteral("UNIQUE NEEDLE")); QTest::mouseClick(search, Qt::LeftButton);
            QVERIFY(window.findChild<QProgressBar *>(QStringLiteral("filterProgress"))->isVisible());
            QTRY_COMPARE(view->model()->rowCount(), 1); QCOMPARE(range->text(), QStringLiteral("1 - 1 / 1件")); QVERIFY(!page->isEnabled());
            view->setCurrentIndex(view->model()->index(0, 3));
            auto *body = window.findChild<QTextBrowser *>(QStringLiteral("messageBody"));
            QTRY_VERIFY(body->toPlainText().contains(QStringLiteral("unique needle body")));
            const auto clickBadge = [&](int column) { const auto index = view->model()->index(0, column); QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(index).center()); };
            clickBadge(0); QTRY_VERIFY(!server.messageFlags.value(count).contains("\\Flagged")); QTRY_VERIFY(view->model()->index(0, 0).data().toString().isEmpty());
            clickBadge(1); QTRY_VERIFY(!server.messageFlags.value(count).contains("\\Seen")); QTRY_COMPARE(view->model()->index(0, 1).data().toString(), QStringLiteral("●"));
            QTRY_VERIFY(folders->model()->index(0, 0).data().toString().endsWith(QStringLiteral("(3345)")));
            server.rejectFlagChanges = true; clickBadge(0);
            QTRY_VERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("failed")));
            QVERIFY(view->model()->index(0, 0).data().toString().isEmpty()); server.rejectFlagChanges = false;
            capture(QStringLiteral("12-large-mailbox-filter-and-flags"));
            filter->clear(); QTest::mouseClick(search, Qt::LeftButton);
            auto *important = window.findChild<QCheckBox *>(QStringLiteral("importantOnly"));
            auto *unread = window.findChild<QCheckBox *>(QStringLiteral("unreadOnly"));
            important->setChecked(true); unread->setChecked(true);
            QTRY_COMPARE(range->text(), QStringLiteral("1 - 100 / 1672件"));
            // Stale intermediate query results cannot overwrite the latest filter.
            filter->setText(QStringLiteral("absent fixture")); QTest::mouseClick(search, Qt::LeftButton);
            QTRY_COMPARE(range->text(), QStringLiteral("0 - 0 / 0件")); QCOMPARE(view->model()->rowCount(), 0); QVERIFY(!page->isEnabled());
            window.revealMessage(account.id, QStringLiteral("INBOX"), 10, 1);
            QTRY_VERIFY(window.selectedMessage().has_value()); QTRY_COMPARE(window.selectedMessage()->uid, 1);
            QCOMPARE(page->value(), 101); // Source navigation can cross pages and clear filters.
        }
        for (const auto &protocol : {QStringLiteral("imap"), QStringLiteral("smtp")}) {
            bool done = false; credentials.remove(AccountRegistrar::secretKey(account.id, protocol), [&](const QString &, const QString &) { done = true; }); QTRY_VERIFY(done);
        }
    }
    void failedEditRestoresExistingCredentials()
    {
        AccountRepository repository(QStringLiteral(":memory:"));
        CredentialStore credentials(nullptr, QStringLiteral("SmoothMail disposable registrar test"));
        AccountRegistrar registrar(&repository, &credentials);
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("rollback@example.test"),
            {QStringLiteral("smtp.example.test"), 465, QStringLiteral("test")},
            {QStringLiteral("imap.example.test"), 993, QStringLiteral("test")}};
        for (const auto &protocol : {QStringLiteral("smtp"), QStringLiteral("imap")}) {
            bool done = false; QString error;
            credentials.write(AccountRegistrar::secretKey(account.id, protocol), QStringLiteral("old-dummy"),
                [&](const QString &, const QString &value) { error = value; done = true; });
            QTRY_VERIFY(done); QVERIFY(error.isEmpty());
        }
        QSignalSpy result(&registrar, &AccountRegistrar::finished);
        // No corresponding DB row: credentials are written, UPDATE fails,
        // and both pre-existing OS values must be restored, not deleted.
        registrar.edit(account, QStringLiteral("new-dummy"), QStringLiteral("new-dummy"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000);
        QVERIFY(!result.first().at(1).toString().isEmpty());
        for (const auto &protocol : {QStringLiteral("smtp"), QStringLiteral("imap")}) {
            bool done = false; QString value, error;
            const auto key = AccountRegistrar::secretKey(account.id, protocol);
            credentials.read(key, [&](const QString &secret, const QString &failure) { value = secret; error = failure; done = true; });
            QTRY_VERIFY(done); QVERIFY(error.isEmpty()); QCOMPARE(value, QStringLiteral("old-dummy"));
            done = false;
            credentials.remove(key, [&](const QString &, const QString &failure) { error = failure; done = true; });
            QTRY_VERIFY(done); QVERIFY(error.isEmpty());
        }
    }
};
QTEST_MAIN(GuiSmokeTest)
#include "tst_guismoke.moc"
