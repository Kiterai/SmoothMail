#include "app/applicationcontroller.h"
#include "mailtestserver.h"
#include "ui/accountdialog.h"
#include "ui/composedialog.h"
#include "ui/downloadhistorydialog.h"
#include "ui/mainwindow.h"
#include "ui/systemsettingsdialog.h"
#include <QSettings>
#include "storage/accountregistrar.h"
#include "mime/mimeparser.h"
#include "mail/mailservice.h"
#include <algorithm>
#include <QElapsedTimer>
#include "core/diagnostics.h"

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
#include <memory>
#include <vector>

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
        QList<QModelIndex> parents{QModelIndex{}};
        while (!parents.isEmpty()) {
            const auto parent = parents.takeLast();
            for (int row = 0; row < view->model()->rowCount(parent); ++row) {
                const auto index = view->model()->index(row, 3, parent);
                if (index.data().toString() == subject) return index;
                parents.append(index.siblingAtColumn(0));
            }
        }
        return {};
    }
    static void contextAction(QTreeView *view, const QModelIndex &index, const QString &text)
    {
        QVERIFY(index.isValid());
        // A matched model index may live under the collapsed "Folders" group.
        // An empty visualRect otherwise sends the context click to another row.
        for (auto parent = index.parent(); parent.isValid(); parent = parent.parent())
            view->expand(parent);
        view->scrollTo(index);
        const auto position = view->visualRect(index).center();
        QCOMPARE(view->indexAt(position), index);
        QTimer::singleShot(100, view, [text] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto *action : menu->actions()) if (action->text() == text) {
                menu->setActiveAction(action); QTest::keyClick(menu, Qt::Key_Return); return;
            }
            menu->close();
        });
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
    void controllerSharesSpareFolderSlotsWithinOneUpdate()
    {
        QSettings().clear(); QSettings().setValue(QStringLiteral("refresh/parallelUpdates"), 4);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("folder-slots.sqlite"));
        std::vector<std::unique_ptr<MailTestServer>> servers; QList<Account> accounts;
        for (int i = 0; i < 3; ++i) {
            auto server = std::make_unique<MailTestServer>(MailTestServer::Protocol::Imap, QStringList{"PLAIN"});
            server->provideMailbox = true; server->bodyCompletionDelayMs = 250;
            server->messages[1] = "Subject: primary\r\n\r\nbody";
            for (int folder = 0; folder < 12; ++folder) {
                const auto name = "Folder-" + QByteArray::number(folder); server->extraFolders.insert(name);
                server->folderMessages[name][1] = "Subject: folder\r\n\r\nbody";
            }
            QVERIFY(server->listen());
            accounts.append({QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("slots%1@example.test").arg(i),
                {"127.0.0.1", 465, "test"}, {"127.0.0.1", server->port(), "test", AuthenticationMode::Plain, EncryptionMode::None}});
            servers.push_back(std::move(server));
        }
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted);
            for (const auto &account : accounts) {
                repository.insert(account); QTRY_COMPARE(inserted.size(), 1); inserted.clear();
                QVERIFY(repository.beginSync(account.id, "INBOX", 10).isEmpty());
                QVERIFY(repository.commitSync({{"INBOX", u'/', {}, 0}}, 0).isEmpty()); repository.abortSync();
            }
        }
        CredentialStore credentials;
        for (const auto &account : accounts) {
            bool done = false;
            credentials.write(AccountRegistrar::secretKey(account.id, "imap", AuthenticationMode::Plain), "dummy",
                [&](const QString &, const QString &error) { QVERIFY(error.isEmpty()); done = true; });
            QTRY_VERIFY(done);
        }
        {
            MainWindow window(false); ApplicationController controller(&window, path, qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
            window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto *tree = window.findChild<QTreeView *>("folderTree"); QTRY_COMPARE(tree->model()->rowCount(), 3);
            QSet<QString> finished; QStringList errors;
            for (auto *mail : controller.findChildren<MailService *>()) connect(mail, &MailService::synchronized, &window,
                [&](const QString &id, const QString &, const QList<MailFolder> &, const QList<MailRecord> &, const QString &error) {
                    finished.insert(id); if (!error.isEmpty() && id != accounts[0].id) errors.append(error);
                });
            int maximum = 0;
            QTimer monitor; monitor.setInterval(5);
            connect(&monitor, &QTimer::timeout, &window, [&] {
                int count = 0; for (const auto &server : servers) count += server->liveConnections;
                maximum = qMax(maximum, count);
            }); monitor.start();
            window.fullRefreshRequested();
            QTRY_VERIFY_WITH_TIMEOUT(servers[0]->maximumConnections >= 2, 15000);
            QCOMPARE(servers[1]->maximumConnections, 1); // Base for every account, surplus to the top one.
            window.targetRefreshRequested(accounts[2].id, {}, true);
            QTRY_VERIFY_WITH_TIMEOUT(servers[2]->receivedAuthentication(), 15000);
            QVERIFY(!finished.contains(accounts[0].id));
            action(window, "systemSettingsAction")->trigger();
            QPointer<SystemSettingsDialog> settings = window.findChild<SystemSettingsDialog *>(); QVERIFY(settings);
            settings->findChild<QSpinBox *>("settingsParallelUpdates")->setValue(2);
            settings->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); QTRY_VERIFY(settings.isNull());
            window.deleteAccountRequested(accounts[0].id); // Cancel primary and helper sessions together.
            QTRY_COMPARE(tree->model()->rowCount(), 2);
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 30000);
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
            QVERIFY(maximum <= 4);
        }
        for (const auto &account : accounts) {
            bool done = false; credentials.remove(AccountRegistrar::secretKey(account.id, "imap", AuthenticationMode::Plain),
                [&](const QString &, const QString &) { done = true; }); QTRY_VERIFY(done);
        }
        QSettings().clear();
    }
    void initTestCase()
    {
        if (qEnvironmentVariable("SMOOTHMAIL_GUI_SANDBOX_TEST") != QStringLiteral("1"))
            QSKIP("Run only in the disposable Windows Sandbox; this test saves dummy guest credentials and an attachment.");
        QCoreApplication::setQuitLockEnabled(false);
        QCoreApplication::setOrganizationName(QStringLiteral("SmoothMail GUI Sandbox"));
        QCoreApplication::setApplicationName(QStringLiteral("SmoothMail GUI Sandbox"));
        QStandardPaths::setTestModeEnabled(true);
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
        QVERIFY(diagnostics::start(qEnvironmentVariable("SMOOTHMAIL_GUI_OUTPUT") + QStringLiteral("/gui-diagnostics")));
    }
    void cleanupTestCase() { diagnostics::finish(); }
    void parallelControllerHonorsLimitChangesDeletionAndFailure()
    {
        QSettings().clear(); QSettings().setValue(QStringLiteral("refresh/parallelUpdates"), 2);
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath(QStringLiteral("parallel.sqlite"));
        std::vector<std::unique_ptr<MailTestServer>> servers; QList<Account> accounts;
        for (int i = 0; i < 4; ++i) {
            auto server = std::make_unique<MailTestServer>(MailTestServer::Protocol::Imap, QStringList{QStringLiteral("PLAIN")});
            server->provideMailbox = true; server->pauseMetadata = true;
            server->messages.insert(1, "Subject: parallel " + QByteArray::number(i) + "\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n\r\nbody");
            QVERIFY(server->listen());
            accounts.append({QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("parallel%1@example.test").arg(i),
                {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
                {QStringLiteral("127.0.0.1"), server->port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}});
            servers.push_back(std::move(server));
        }
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted);
            for (const auto &account : accounts) {
                repository.insert(account); QTRY_COMPARE(inserted.size(), 1); inserted.clear();
                QVERIFY(repository.beginSync(account.id, QStringLiteral("INBOX"), 10).isEmpty());
                QVERIFY(repository.commitSync({{QStringLiteral("INBOX"), u'/', {}, 0}}, 0).isEmpty()); repository.abortSync();
            }
        }
        CredentialStore credentials;
        for (const auto &account : accounts) {
            bool done = false; QString error;
            credentials.write(AccountRegistrar::secretKey(account.id, QStringLiteral("imap"), AuthenticationMode::Plain), QStringLiteral("dummy"),
                [&](const QString &, const QString &result) { error = result; done = true; }); QTRY_VERIFY(done); QVERIFY(error.isEmpty());
        }
        {
            MainWindow window(false); ApplicationController controller(&window, path, qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
            window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto *tree = window.findChild<QTreeView *>(QStringLiteral("folderTree")); QTRY_COMPARE(tree->model()->rowCount(), 4);
            QTRY_VERIFY_WITH_TIMEOUT(([&] { action(window, "systemSettingsAction")->trigger(); return window.findChild<SystemSettingsDialog *>() != nullptr; })(), 10000);
            window.findChild<SystemSettingsDialog *>()->close();
            QHash<QString, QString> results;
            for (auto *mail : controller.findChildren<MailService *>()) connect(mail, &MailService::synchronized, &window,
                [&](const QString &id, const QString &, const QList<MailFolder> &, const QList<MailRecord> &, const QString &error) { results[id] = error; });
            window.fullRefreshRequested();
            QTRY_VERIFY_WITH_TIMEOUT(servers[0]->pausedMetadataCount() > 0 && servers[1]->pausedMetadataCount() > 0, 15000);
            QTest::qWait(150); QVERIFY(!servers[2]->receivedAuthentication()); QVERIFY(!servers[3]->receivedAuthentication());
            // Decreasing the limit drains existing work without cancelling it.
            action(window, "systemSettingsAction")->trigger();
            QPointer<SystemSettingsDialog> settings = window.findChild<SystemSettingsDialog *>(); QVERIFY(settings);
            settings->findChild<QSpinBox *>(QStringLiteral("settingsParallelUpdates"))->setValue(1);
            settings->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); QTRY_VERIFY(settings.isNull());
            servers[0]->disconnectOnBodyFetch = true; servers[0]->resumeMetadata();
            QTRY_VERIFY_WITH_TIMEOUT(results.contains(accounts[0].id), 15000); QVERIFY(!results.value(accounts[0].id).isEmpty());
            QVERIFY(!servers[2]->receivedAuthentication()); QVERIFY(!servers[3]->receivedAuthentication());
            // Delete only the paused account; the next account must progress.
            window.deleteAccountRequested(accounts[1].id);
            QTRY_COMPARE(tree->model()->rowCount(), 3);
            QTRY_VERIFY_WITH_TIMEOUT(servers[2]->pausedMetadataCount() > 0, 15000);
            QVERIFY(!servers[3]->receivedAuthentication());
            action(window, "systemSettingsAction")->trigger(); settings = window.findChild<SystemSettingsDialog *>(); QVERIFY(settings);
            settings->findChild<QSpinBox *>(QStringLiteral("settingsParallelUpdates"))->setValue(2);
            settings->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); QTRY_VERIFY(settings.isNull());
            QTRY_VERIFY_WITH_TIMEOUT(servers[3]->pausedMetadataCount() > 0, 15000);
            servers[3]->resumeMetadata(); servers[2]->resumeMetadata();
            QTRY_VERIFY_WITH_TIMEOUT(results.contains(accounts[2].id) && results.contains(accounts[3].id), 15000);
            QVERIFY(results.value(accounts[2].id).isEmpty()); QVERIFY(results.value(accounts[3].id).isEmpty());
        }
        {
            AccountRepository repository(path); QSignalSpy pages(&repository, &AccountRepository::pageLoaded);
            for (int i : {2, 3}) {
                MailQuery query; query.accountId = accounts[i].id; query.folder = QStringLiteral("INBOX"); repository.loadPage(query);
                QTRY_COMPARE(pages.size(), 1); const auto page = qvariant_cast<MailPage>(pages.takeFirst().first());
                QCOMPARE(page.total, 1); QCOMPARE(page.messages.first().document.subject, QStringLiteral("parallel %1").arg(i));
            }
        }
        for (const auto &account : accounts) {
            bool done = false; credentials.remove(AccountRegistrar::secretKey(account.id, QStringLiteral("imap"), AuthenticationMode::Plain),
                [&](const QString &, const QString &) { done = true; }); QTRY_VERIFY(done);
        }
        QSettings().clear();
    }
    void cancelledInitialFetchDoesNotRestartWhenPartialCacheLoads()
    {
        QSettings().clear(); QTemporaryDir directory; QVERIFY(directory.isValid());
        MailTestServer server(MailTestServer::Protocol::Imap, {"PLAIN"});
        server.provideMailbox = true; server.bodyCompletionDelayMs = 5000;
        server.messages[1] = "Subject: retained after stop\r\n\r\nbody"; QVERIFY(server.listen());
        const Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), "cancel-initial@example.test",
            {"127.0.0.1", 465, "test"}, {"127.0.0.1", server.port(), "test", AuthenticationMode::Plain, EncryptionMode::None}};
        const auto path = directory.filePath("cancel-initial.sqlite");
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted);
            repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
        }
        CredentialStore credentials; bool done = false;
        const auto key = AccountRegistrar::secretKey(account.id, "imap", AuthenticationMode::Plain);
        credentials.write(key, "dummy", [&](const QString &, const QString &error) { QVERIFY(error.isEmpty()); done = true; }); QTRY_VERIFY(done);
        {
            MainWindow window(false); ApplicationController controller(&window, path, qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
            window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
            QTRY_COMPARE(window.findChild<QTreeView *>("folderTree")->model()->rowCount(), 1);
            window.fullRefreshRequested();
            QTRY_COMPARE_WITH_TIMEOUT(server.fullFetchCount, 1, 15000);
            QTest::qWait(150); // Complete response is buffered; tagged completion is still delayed.
            action(window, "cancelRefreshAction")->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(!action(window, "cancelRefreshAction")->isEnabled(), 10000);
            auto *repository = controller.findChild<AccountRepository *>(); QVERIFY(repository);
            QSignalSpy pages(repository, &AccountRepository::pageLoaded);
            repository->loadPage({account.id, "INBOX"}); QTRY_COMPARE(pages.size(), 1);
            QCOMPARE(qvariant_cast<MailPage>(pages.first().first()).total, 1);
            QTest::qWait(500); // Allow the partial cache reload/initial-fetch callback chain to run.
            QCOMPARE(server.authenticationResponses.size(), 1);
            QVERIFY(!action(window, "cancelRefreshAction")->isEnabled());
        }
        done = false; credentials.remove(key, [&](const QString &, const QString &) { done = true; }); QTRY_VERIFY(done);
        QSettings().clear();
    }
    void scopedControllerRejectsOverlapAndCancelsTheUpdate()
    {
        QSettings().clear(); QTemporaryDir directory; QVERIFY(directory.isValid());
        MailTestServer first(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        MailTestServer second(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        for (auto *server : {&first, &second}) {
            server->provideMailbox = true; server->extraFolders.insert("Other");
            server->messages.insert(1, "Subject: scope fixture\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n\r\nbody");
            QVERIFY(server->listen());
        }
        QList<Account> accounts;
        for (auto *server : {&first, &second}) accounts.append({
            QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("scope@example.test"),
            {QStringLiteral("127.0.0.1"), 465, QStringLiteral("test")},
            {QStringLiteral("127.0.0.1"), server->port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}});
        const auto path = directory.filePath(QStringLiteral("scope.sqlite"));
        {
            AccountRepository repository(path); MailService service(qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
            QSignalSpy inserted(&repository, &AccountRepository::inserted), synchronized(&service, &MailService::synchronized);
            for (const auto &account : accounts) {
                repository.insert(account); QTRY_COMPARE(inserted.size(), 1); inserted.clear();
                service.synchronize(account, QStringLiteral("dummy"), {}, &repository, MailService::SyncMode::All);
                QTRY_COMPARE_WITH_TIMEOUT(synchronized.size(), 1, 15000); QVERIFY(synchronized.takeFirst().at(4).toString().isEmpty());
            }
        }
        CredentialStore credentials;
        for (const auto &account : accounts) {
            bool done = false; QString error;
            credentials.write(AccountRegistrar::secretKey(account.id, QStringLiteral("imap"), AuthenticationMode::Plain),
                QStringLiteral("dummy"), [&](const QString &, const QString &result) { error = result; done = true; });
            QTRY_VERIFY(done); QVERIFY(error.isEmpty());
        }
        {
            MainWindow window(false); ApplicationController controller(&window, path, qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
            window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto *tree = window.findChild<QTreeView *>(QStringLiteral("folderTree"));
            QTRY_COMPARE(tree->model()->rowCount(), 2);
            QTRY_VERIFY_WITH_TIMEOUT(([&] { action(window, "systemSettingsAction")->trigger(); return window.findChild<SystemSettingsDialog *>() != nullptr; })(), 10000);
            window.findChild<SystemSettingsDialog *>()->close();
            first.commands.clear(); second.commands.clear();
            QStringList errors; int completed = 0;
            for (auto *mail : controller.findChildren<MailService *>()) connect(mail, &MailService::synchronized, &window,
                [&](const QString &, const QString &, const QList<MailFolder> &, const QList<MailRecord> &, const QString &error) {
                    errors.append(error); ++completed;
                });
            // Neither a different scope nor a full refresh may stack onto an update.
            window.targetRefreshRequested(accounts[0].id, QStringLiteral("INBOX"), false);
            window.targetRefreshRequested(accounts[0].id, QStringLiteral("Other"), false);
            window.targetRefreshRequested(accounts[0].id, QStringLiteral("Other"), true);
            window.targetRefreshRequested(accounts[1].id, {}, true);
            const auto count = [](const MailTestServer &server, const QByteArray &needle) {
                int n = 0; for (const auto &command : server.commands) if (command.contains(needle)) ++n; return n;
            };
            QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 30000);
            for (const auto &error : errors) QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(count(first, "STATUS \"INBOX\""), 1); QCOMPARE(count(first, "STATUS \"Other\""), 0);
            QVERIFY(second.commands.isEmpty());
            QVERIFY(action(window, "refreshAction")->isEnabled());
            QVERIFY(!action(window, "cancelRefreshAction")->isEnabled());
            // Once idle, another explicit request is a normal new update.
            window.targetRefreshRequested(accounts[1].id, {}, true);
            QTRY_COMPARE_WITH_TIMEOUT(completed, 2, 30000);
            QCOMPARE(count(second, "STATUS \"INBOX\""), 1); QCOMPARE(count(second, "STATUS \"Other\""), 1);
            first.pauseMetadata = true; second.pauseMetadata = true;
            window.fullRefreshRequested();
            QTRY_VERIFY_WITH_TIMEOUT(first.pausedMetadataCount() > 0 && second.pausedMetadataCount() > 0, 15000);
            QVERIFY(!action(window, "refreshAction")->isEnabled()); QVERIFY(!action(window, "fullRefreshAction")->isEnabled());
            QVERIFY(action(window, "cancelRefreshAction")->isEnabled());
            const int authentications = first.authenticationResponses.size() + second.authenticationResponses.size();
            window.fullRefreshRequested(); window.targetRefreshRequested(accounts[0].id, {}, true);
            action(window, "cancelRefreshAction")->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(!action(window, "cancelRefreshAction")->isEnabled(), 10000);
            QCOMPARE(completed, 4); QCOMPARE(first.authenticationResponses.size() + second.authenticationResponses.size(), authentications);
            QVERIFY(action(window, "refreshAction")->isEnabled()); QVERIFY(action(window, "fullRefreshAction")->isEnabled());
        }
        for (const auto &account : accounts) {
            bool done = false;
            credentials.remove(AccountRegistrar::secretKey(account.id, QStringLiteral("imap"), AuthenticationMode::Plain),
                [&](const QString &, const QString &) { done = true; }); QTRY_VERIFY(done);
        }
        QSettings().clear();
    }
    void settingsPersistAndLocalMovesHaveManualOnlyUndo()
    {
        QSettings().clear(); QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath(QStringLiteral("settings-moves.sqlite"));
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("local@example.test"), {QStringLiteral("offline.example.test"), 465, QStringLiteral("test")}, {QStringLiteral("offline.example.test"), 993, QStringLiteral("test")}};
        const auto parsed = MimeParser::parse("Subject: local fixture\r\n\r\nbody"); const MailRecord identity{account.id, QStringLiteral("INBOX"), 10, 1, false, false, parsed.message};
        const QList<MailFolder> folders{{QStringLiteral("INBOX"), u'/', {}, 1}, {QStringLiteral("Target"), u'/', {}, 0}};
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved); repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
            repository.saveSnapshot(account.id, QStringLiteral("INBOX"), folders, {identity}); QTRY_COMPARE(saved.size(), 1);
            repository.saveSnapshot(account.id, QStringLiteral("Target"), folders, {}); QTRY_COMPARE(saved.size(), 2);
        }
        {
            MainWindow window(false); ApplicationController controller(&window, path, qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto *view = window.findChild<QTreeView *>(QStringLiteral("messageList")); QTRY_COMPARE(view->model()->rowCount(), 1);
            // Startup secret load is asynchronous. Retry opening until the guest keychain is ready.
            QTRY_VERIFY_WITH_TIMEOUT(([&] { action(window, "systemSettingsAction")->trigger(); return window.findChild<SystemSettingsDialog *>() != nullptr; })(), 10000);
            QPointer<SystemSettingsDialog> dialog = window.findChild<SystemSettingsDialog *>();
            dialog->findChild<QSpinBox *>(QStringLiteral("settingsPageSize"))->setValue(17); dialog->findChild<QSpinBox *>(QStringLiteral("settingsInterval"))->setValue(45);
            dialog->findChild<QSpinBox *>(QStringLiteral("settingsParallelUpdates"))->setValue(3);
            dialog->findChild<QLineEdit *>(QStringLiteral("settingsGoogleClient"))->setText(QStringLiteral("dummy-client")); dialog->findChild<QLineEdit *>(QStringLiteral("settingsGoogleSecret"))->setText(QStringLiteral("dummy-system-secret"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); QTRY_VERIFY(dialog.isNull());
            QVERIFY(!action(window, "undoAction")->isEnabled());
            window.automaticFlagRequested(identity, false, true); QTRY_COMPARE(view->model()->index(0, 1).data().toString(), QStringLiteral("○")); QVERIFY(!action(window, "undoAction")->isEnabled());
            window.messageFlagRequested(identity, true, true); QTRY_COMPARE(view->model()->index(0, 0).data().toString(), QStringLiteral("★"));
            view->setFocus(); action(window, "undoAction")->trigger(); QTRY_COMPARE(view->model()->index(0, 0).data().toString(), QStringLiteral("☆")); QCOMPARE(view->model()->index(0, 1).data().toString(), QStringLiteral("○"));
            action(window, "redoAction")->trigger(); QTRY_COMPARE(view->model()->index(0, 0).data().toString(), QStringLiteral("★"));
            window.messageMoveRequested(identity, QStringLiteral("Target")); QTRY_COMPARE(view->model()->rowCount(), 0);
            action(window, "undoAction")->trigger(); QTRY_COMPARE(view->model()->rowCount(), 1);
            window.folderOperationRequested(account.id, QStringLiteral("create"), {}, QStringLiteral("New")); auto *tree = window.findChild<QTreeView *>(QStringLiteral("folderTree")); QTRY_VERIFY(folderIndex(tree, QStringLiteral("New")).isValid());
            action(window, "undoAction")->trigger(); QTRY_VERIFY(!folderIndex(tree, QStringLiteral("New")).isValid());
            action(window, "redoAction")->trigger(); QTRY_VERIFY(folderIndex(tree, QStringLiteral("New")).isValid());
            window.folderMoveRequested(account.id, QStringLiteral("Target"), QStringLiteral("INBOX"), u'/'); QTRY_VERIFY(folderIndex(tree, QStringLiteral("INBOX/Target")).isValid());
            action(window, "undoAction")->trigger(); QTRY_VERIFY(folderIndex(tree, QStringLiteral("Target")).isValid()); QVERIFY(!folderIndex(tree, QStringLiteral("INBOX/Target")).isValid());
        }
        {
            MainWindow window(false); ApplicationController controller(&window, path, qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH"));
            QTRY_VERIFY_WITH_TIMEOUT(([&] { action(window, "systemSettingsAction")->trigger(); return window.findChild<SystemSettingsDialog *>() != nullptr; })(), 10000);
            auto *dialog = window.findChild<SystemSettingsDialog *>(); QCOMPARE(dialog->findChild<QSpinBox *>(QStringLiteral("settingsPageSize"))->value(), 17); QCOMPARE(dialog->findChild<QSpinBox *>(QStringLiteral("settingsInterval"))->value(), 45);
            QCOMPARE(dialog->findChild<QSpinBox *>(QStringLiteral("settingsParallelUpdates"))->value(), 3);
            // Compare the dummy secret, never print it in diagnostics or a failed QCOMPARE.
            QVERIFY(dialog->findChild<QLineEdit *>(QStringLiteral("settingsGoogleSecret"))->text() == QStringLiteral("dummy-system-secret"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        }
        QSettings().clear(); CredentialStore credentials; bool done = false; credentials.remove(QStringLiteral("system/google-client-secret"), [&](const QString &, const QString &) { done = true; }); QTRY_VERIFY(done);
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
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QTest::mouseClick(account->findChild<QPushButton *>(QStringLiteral("connectionTest")), Qt::LeftButton);
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
        QCOMPARE(messages->treePosition(), 3);
        QCOMPARE(messages->model()->rowCount(messages->model()->index(0, 0)), 1);
        messages->expandAll();
        const auto replyIndex = subjectIndex(messages, QStringLiteral("Sandbox reply"));
        QVERIFY(replyIndex.isValid()); QVERIFY(!messages->visualRect(replyIndex).isEmpty());
        QCOMPARE(messages->indexAt(messages->visualRect(replyIndex).center()), replyIndex);
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
        compose->findChild<QPlainTextEdit *>(QStringLiteral("composeBody"))->setPlainText(QStringLiteral("preserved reply draft"));
        auto *filter = window.findChild<QLineEdit *>(QStringLiteral("mailFilter")); filter->setText(QStringLiteral("no matching fixture"));
        window.findChild<QPushButton *>(QStringLiteral("mailSearch"))->click(); QTRY_COMPARE(messages->model()->rowCount(), 0);
        auto *replySource = compose->findChild<QLabel *>(QStringLiteral("composeReplySource")); QVERIFY(replySource);
        QCOMPARE(replySource->text(), subject); QTest::mouseClick(replySource, Qt::LeftButton);
        QTRY_VERIFY(compose->isHidden()); QTRY_COMPARE(messages->currentIndex().siblingAtColumn(3).data().toString(), subject);
        QTRY_VERIFY(body->toPlainText().contains(QStringLiteral("Sandbox plain body"))); QVERIFY(filter->text().isEmpty());
        const auto sameDialog = compose; action(window, "composeAction")->trigger();
        QCOMPARE(compose, sameDialog); QTRY_VERIFY(compose->isVisible());
        QCOMPARE(window.findChild<ComposeDialog *>(), compose.data());
        QCOMPARE(compose->draft().body, QStringLiteral("preserved reply draft"));
        QTest::mouseMove(replySource, replySource->rect().center());
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
        QVERIFY(!account->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
        QTest::mouseClick(account->findChild<QPushButton *>(QStringLiteral("connectionTest")), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(account->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(), 15000);
        capture(QStringLiteral("09-account-edit"));
        QTest::mouseClick(account->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(account.isNull(), 15000);
        QTRY_VERIFY(folders->model()->index(0, 0).data().toString().startsWith(QStringLiteral("edited@example.test")));
        action(window, "refreshAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        contextAction(folders, folders->model()->index(0, 0), QStringLiteral("新規フォルダ"));
        QTRY_VERIFY_WITH_TIMEOUT(folders->findChild<QLineEdit *>(), 15000);
        auto *inlineEditor = folders->findChild<QLineEdit *>(); QVERIFY(inlineEditor);
        inlineEditor->setText(QStringLiteral("Parent")); QTest::keyClick(inlineEditor, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(folderIndex(folders, QStringLiteral("Parent")).isValid(), 10000);
        QVERIFY(!imap.extraFolders.contains("Parent")); action(window, "refreshAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(imap.extraFolders.contains("Parent"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        contextAction(folders, folderIndex(folders, QStringLiteral("Parent")), QStringLiteral("新規フォルダ"));
        QTRY_VERIFY_WITH_TIMEOUT(folders->findChild<QLineEdit *>(), 15000);
        inlineEditor = folders->findChild<QLineEdit *>(); QVERIFY(inlineEditor);
        inlineEditor->setText(QStringLiteral("Child")); QTest::keyClick(inlineEditor, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(folderIndex(folders, QStringLiteral("Parent/Child")).isValid(), 10000);
        QVERIFY(!imap.extraFolders.contains("Parent/Child")); action(window, "refreshAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(imap.extraFolders.contains("Parent/Child"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        contextAction(folders, folderIndex(folders, QStringLiteral("Parent/Child")), QStringLiteral("名前の変更"));
        inlineEditor = folders->findChild<QLineEdit *>(); QVERIFY(inlineEditor);
        inlineEditor->setText(QStringLiteral("Renamed")); QTest::keyClick(inlineEditor, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(folderIndex(folders, QStringLiteral("Parent/Renamed")).isValid(), 10000);
        QVERIFY(imap.extraFolders.contains("Parent/Child")); action(window, "refreshAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(imap.extraFolders.contains("Parent/Renamed"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        capture(QStringLiteral("10-folder-edit"));
        // The warning defaults to Cancel; cancellation must issue no DELETE.
        QTimer::singleShot(200, &window, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->reject(); });
        contextAction(folders, folderIndex(folders, QStringLiteral("Parent/Renamed")), QStringLiteral("削除"));
        QVERIFY(imap.extraFolders.contains("Parent/Renamed"));
        QTimer::singleShot(200, &window, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->done(QMessageBox::Yes); });
        contextAction(folders, folderIndex(folders, QStringLiteral("Parent/Renamed")), QStringLiteral("削除"));
        QTRY_VERIFY(!folderIndex(folders, QStringLiteral("Parent/Renamed")).isValid());
        QVERIFY(imap.extraFolders.contains("Parent/Renamed")); action(window, "refreshAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!imap.extraFolders.contains("Parent/Renamed"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        QTimer::singleShot(200, &window, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->done(QMessageBox::Yes); });
        contextAction(folders, folders->model()->index(0, 0), QStringLiteral("アカウントの削除"));
        QTRY_COMPARE_WITH_TIMEOUT(folders->model()->rowCount(), 0, 10000);
        QCOMPARE(imap.messages.size(), 2); // Account deletion never deletes server mail.
    }
    void editsFoldersSendsMailAndDeletesAccountDuringFetch()
    {
        MailTestServer oldServer(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")}), nextServer(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        MailTestServer smtp(MailTestServer::Protocol::Smtp, {QStringLiteral("PLAIN")});
        oldServer.provideMailbox = nextServer.provideMailbox = true; oldServer.pauseMetadata = true;
        oldServer.messages.insert(1, "Subject: old snapshot\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n\r\nold body");
        nextServer.messages.insert(9, "Subject: new snapshot\r\nDate: Fri, 13 Nov 2026 10:00:00 +0000\r\n\r\nnew body");
        QVERIFY(oldServer.listen()); QVERIFY(nextServer.listen()); QVERIFY(smtp.listen());
        Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("inflight@example.test"),
            {QStringLiteral("127.0.0.1"), smtp.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None},
            {QStringLiteral("127.0.0.1"), oldServer.port(), QStringLiteral("test"), AuthenticationMode::Plain, EncryptionMode::None}};
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath(QStringLiteral("inflight.sqlite"));
        {
            AccountRepository repository(path); QSignalSpy inserted(&repository, &AccountRepository::inserted), saved(&repository, &AccountRepository::snapshotSaved);
            repository.insert(account); QTRY_COMPARE(inserted.size(), 1);
            const auto parsed = MimeParser::parse(oldServer.messages.value(1)); QVERIFY(parsed.ok());
            repository.saveSnapshot(account.id, QStringLiteral("INBOX"), {{QStringLiteral("INBOX"), u'/', {}, 1}}, {{account.id, QStringLiteral("INBOX"), 10, 1, false, false, parsed.message}}); QTRY_COMPARE(saved.size(), 1);
        }
        CredentialStore credentials;
        for (const auto &protocol : {QStringLiteral("imap"), QStringLiteral("smtp")}) { bool done = false; QString error; credentials.write(AccountRegistrar::secretKey(account.id, protocol), QStringLiteral("sandbox-only-dummy"), [&](const QString &, const QString &value) { error = value; done = true; }); QTRY_VERIFY(done); QVERIFY(error.isEmpty()); }
        MainWindow window(false); ApplicationController controller(&window, path, qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH")); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_COMPARE(window.findChild<QTreeView *>(QStringLiteral("messageList"))->model()->rowCount(), 1);
        QVERIFY(oldServer.commands.isEmpty()); action(window, "refreshAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(oldServer.pausedMetadataCount(), 1, 10000);
        auto *folders = window.findChild<QTreeView *>(QStringLiteral("folderTree")); auto *messages = window.findChild<QTreeView *>(QStringLiteral("messageList")); QTRY_COMPARE(messages->model()->rowCount(), 1);
        window.folderOperationRequested(account.id, QStringLiteral("create"), {}, QStringLiteral("Offline")); QTRY_VERIFY(folderIndex(folders, QStringLiteral("Offline")).isValid()); QVERIFY(!oldServer.extraFolders.contains("Offline"));
        action(window, "composeAction")->trigger(); QPointer<ComposeDialog> compose = window.findChild<ComposeDialog *>(); QVERIFY(compose);
        compose->findChild<QLineEdit *>(QStringLiteral("composeTo"))->setText(QStringLiteral("receiver@example.test")); compose->findChild<QLineEdit *>(QStringLiteral("composeSubject"))->setText(QStringLiteral("send during fetch"));
        compose->findChild<QPlainTextEdit *>(QStringLiteral("composeBody"))->setPlainText(QStringLiteral("synthetic body")); QTest::mouseClick(compose->findChild<QPushButton *>(QStringLiteral("composeSend")), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(smtp.deliveredMessages.size(), 1, 10000); QTRY_VERIFY(compose.isNull()); QCOMPARE(oldServer.pausedMetadataCount(), 1);
        contextAction(folders, folders->model()->index(0, 0), QStringLiteral("アカウントの編集")); QPointer<AccountDialog> dialog = window.findChild<AccountDialog *>(); QVERIFY(dialog); QTRY_VERIFY(dialog->isEnabled());
        dialog->findChild<QSpinBox *>(QStringLiteral("IMAP4Port"))->setValue(nextServer.port()); dialog->findChild<QLineEdit *>(QStringLiteral("accountEmail"))->setText(QStringLiteral("changed@example.test"));
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("connectionTest")), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(), 15000); nextServer.commands.clear();
        QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton); QTRY_VERIFY(dialog.isNull());
        QTRY_VERIFY(folders->model()->index(0, 0).data().toString().startsWith(QStringLiteral("changed@example.test"))); QVERIFY(nextServer.commands.isEmpty());
        oldServer.resumeMetadata(); QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000); QVERIFY(nextServer.commands.isEmpty());
        action(window, "refreshAction")->trigger(); QTRY_VERIFY_WITH_TIMEOUT(nextServer.receivedAuthentication(), 10000); QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 15000);
        QTRY_COMPARE(messages->model()->index(0, 3).data().toString(), QStringLiteral("new snapshot")); QVERIFY(!nextServer.extraFolders.contains("Offline"));
        nextServer.pauseMetadata = true; action(window, "refreshAction")->trigger(); QTRY_COMPARE_WITH_TIMEOUT(nextServer.pausedMetadataCount(), 1, 10000);
        QElapsedTimer elapsed; elapsed.start(); window.deleteAccountRequested(account.id);
        QTRY_COMPARE_WITH_TIMEOUT(folders->model()->rowCount(), 0, 3000);
        const auto services = controller.findChildren<MailService *>();
        QTRY_VERIFY_WITH_TIMEOUT(std::none_of(services.cbegin(), services.cend(), [](const MailService *service) { return service->busy(); }), 3000);
        QVERIFY(elapsed.elapsed() < 3000); QCOMPARE(nextServer.messages.size(), 1);
        for (const auto &protocol : {QStringLiteral("imap"), QStringLiteral("smtp")}) { bool done = false; credentials.remove(AccountRegistrar::secretKey(account.id, protocol), [&](const QString &, const QString &) { done = true; }); QTRY_VERIFY(done); }
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
            QTRY_VERIFY(ticks > 5); QVERIFY(server.commands.isEmpty()); QCOMPARE(server.fullFetchCount, 0); // A fetched folder is restored without network traffic.
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
            clickBadge(0); QTRY_COMPARE(view->model()->index(0, 0).data().toString(), QStringLiteral("☆"));
            QVERIFY(server.messageFlags.value(count).contains("\\Flagged"));
            clickBadge(1); QTRY_COMPARE(view->model()->index(0, 1).data().toString(), QStringLiteral("●"));
            QVERIFY(server.messageFlags.value(count).contains("\\Seen"));
            QTRY_VERIFY(folders->model()->index(0, 0).data().toString().endsWith(QStringLiteral("(3345)")));
            server.rejectFlagChanges = true; action(window, "refreshAction")->trigger();
            QTRY_VERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("failed")));
            QCOMPARE(view->model()->index(0, 0).data().toString(), QStringLiteral("☆")); server.rejectFlagChanges = false;
            action(window, "refreshAction")->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(!server.messageFlags.value(count).contains("\\Flagged"), 30000);
            QTRY_VERIFY_WITH_TIMEOUT(!server.messageFlags.value(count).contains("\\Seen"), 30000);
            QTRY_COMPARE_WITH_TIMEOUT(window.statusBar()->currentMessage(), QStringLiteral("更新しました。"), 240000);
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
