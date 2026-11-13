#include "ui/mainwindow.h"
#include "ui/foldermodel.h"

#include <QAction>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeView>
#include <QtTest>
#include <QMimeData>
#include <QHeaderView>
#include <QSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include "ui/systemsettingsdialog.h"
#include <QDialogButtonBox>
#include <QScrollBar>

class MainWindowTest final : public QObject
{
    Q_OBJECT

private slots:
    void messageSelectionKeepsHorizontalScrollPosition()
    {
        using namespace smoothmail;
        MainWindow window(false); Account account; account.id = QStringLiteral("scroll"); account.email = QStringLiteral("scroll@example.test"); window.setAccounts({account});
        QList<MailRecord> records;
        for (int i = 1; i <= 80; ++i) { MailRecord record{account.id, QStringLiteral("INBOX"), 10, i}; record.document.subject = QString(500, u'x'); records.append(record); }
        window.applySnapshot(account.id, QStringLiteral("INBOX"), {{QStringLiteral("INBOX"), u'/', {}, 0}}, records, {});
        window.resize(700, 600); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *view = window.findChild<QTreeView *>(QStringLiteral("messageList")); view->setColumnWidth(3, 1800); QCoreApplication::processEvents();
        auto *bar = view->horizontalScrollBar(); QVERIFY(bar->maximum() > 100);
        for (const int position : {0, 70}) {
            bar->setValue(position); view->setCurrentIndex(view->model()->index(0, 3)); QCOMPARE(bar->value(), position);
            const auto rect = view->visualRect(view->model()->index(1, 3)).intersected(view->viewport()->rect()); QVERIFY(!rect.isEmpty());
            QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center()); QCOMPARE(bar->value(), position);
            for (int i = 0; i < 30; ++i) QTest::keyClick(view, Qt::Key_Down);
            QCOMPARE(bar->value(), position); QVERIFY(view->verticalScrollBar()->value() > 0);
            QTest::keyClick(view, Qt::Key_Up); QCOMPARE(bar->value(), position);
            view->scrollTo(view->model()->index(0, 3)); QCOMPARE(bar->value(), position);
        }
    }
    void configurablePagesNavigateAtListEdges()
    {
        using namespace smoothmail;
        MainWindow window(false); window.setPagedStorage(true); window.applySettings({2, 45, {}});
        Account account; account.id = QStringLiteral("edges"); account.email = QStringLiteral("edges@example.test"); window.setAccounts({account});
        QSignalSpy requests(&window, &MainWindow::pageRequested);
        window.applySnapshot(account.id, QStringLiteral("INBOX"), {{QStringLiteral("INBOX"), u'/', {}, 0}}, {}, {}); QVERIFY(!requests.isEmpty());
        MailPage page; page.query = qvariant_cast<MailQuery>(requests.last().first()); QCOMPARE(page.query.pageSize, 2); page.total = 4;
        page.messages = {{account.id, page.query.folder, 10, 1}, {account.id, page.query.folder, 10, 2}}; window.applyPage(page);
        auto *view = window.findChild<QTreeView *>(QStringLiteral("messageList")); view->setCurrentIndex(view->model()->index(1, 3));
        QTest::keyClick(view, Qt::Key_Down); QCOMPARE(qvariant_cast<MailQuery>(requests.last().first()).page, 2);
        page.query = qvariant_cast<MailQuery>(requests.last().first()); page.messages[0].uid = 3; page.messages[1].uid = 4; window.applyPage(page); QCOMPARE(view->currentIndex().row(), 0);
        QTest::keyClick(view, Qt::Key_Up); QCOMPARE(qvariant_cast<MailQuery>(requests.last().first()).page, 1);
        page.query = qvariant_cast<MailQuery>(requests.last().first()); page.messages[0].uid = 1; page.messages[1].uid = 2; window.applyPage(page); QCOMPARE(view->currentIndex().row(), 1);
        const auto count = requests.size(); view->setCurrentIndex(view->model()->index(0, 3)); QTest::keyClick(view, Qt::Key_Up); QCOMPARE(requests.size(), count);
    }
    void settingsDefaultsAndSaveFailureKeepEdits()
    {
        using namespace smoothmail;
        SystemSettingsDialog dialog({}, {}); QSignalSpy save(&dialog, &SystemSettingsDialog::saveRequested);
        auto *page = dialog.findChild<QSpinBox *>(QStringLiteral("settingsPageSize")); auto *interval = dialog.findChild<QSpinBox *>(QStringLiteral("settingsInterval"));
        auto *buttons = dialog.findChild<QDialogButtonBox *>(); auto *button = buttons->button(QDialogButtonBox::Save);
        QCOMPARE(page->value(), 100); QCOMPARE(interval->value(), 30); QVERIFY(!button->isEnabled());
        page->setValue(17); QVERIFY(button->isEnabled()); button->click(); QCOMPARE(save.size(), 1); QCOMPARE(qvariant_cast<SystemSettings>(save.first().first()).pageSize, 17); QVERIFY(!page->isEnabled());
        dialog.saved(QStringLiteral("fixture failure")); QVERIFY(page->isEnabled()); QCOMPARE(page->value(), 17); QVERIFY(button->isEnabled());
        page->setValue(100); QVERIFY(!button->isEnabled());
        buttons->button(QDialogButtonBox::Cancel)->click(); QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }
    void messageDropRejectsWholeMalformedBatch()
    {
        using namespace smoothmail;
        FolderModel folders; auto *account = new QStandardItem(QStringLiteral("account")); account->setData(QStringLiteral("id"), Qt::UserRole); folders.appendRow(account);
        auto *target = new QStandardItem(QStringLiteral("Target")); target->setData(QStringLiteral("id"), Qt::UserRole); target->setData(QStringLiteral("Target"), Qt::UserRole + 1); account->appendRow(target);
        MessageListModel messages; messages.records = {{QStringLiteral("id"), QStringLiteral("INBOX"), 10, 1}, {QStringLiteral("id"), QStringLiteral("INBOX"), 10, -1}};
        for (int i = 0; i < 2; ++i) { auto *item = new QStandardItem(QString::number(i)); item->setData(i, Qt::UserRole); messages.appendRow(item); }
        QSignalSpy moved(&folders, &FolderModel::messageMoveRequested), started(&folders, &FolderModel::messageMoveBatchStarted), finished(&folders, &FolderModel::messageMoveBatchFinished);
        QScopedPointer<QMimeData> invalid(messages.mimeData({messages.index(0, 0), messages.index(1, 0)}));
        QVERIFY(!folders.dropMimeData(invalid.data(), Qt::CopyAction, -1, 0, target->index())); QCOMPARE(moved.size(), 0); QCOMPARE(started.size(), 0);
        messages.records[1].uid = 2; QScopedPointer<QMimeData> valid(messages.mimeData({messages.index(0, 0), messages.index(1, 0)}));
        QVERIFY(folders.dropMimeData(valid.data(), Qt::CopyAction, -1, 0, target->index())); QCOMPARE(moved.size(), 2); QCOMPARE(started.size(), 1); QCOMPARE(finished.size(), 1);
        target->setData(QStringLiteral("another"), Qt::UserRole); QVERIFY(!folders.dropMimeData(valid.data(), Qt::CopyAction, -1, 0, target->index())); QCOMPARE(moved.size(), 2);
    }
    void refreshActionsAndOptimisticBadges()
    {
        using namespace smoothmail;
        MainWindow window(false);
        Account account; account.id = QStringLiteral("flags"); account.email = QStringLiteral("flags@example.test"); window.setAccounts({account});
        MailRecord first{account.id, QStringLiteral("INBOX"), 10, 1}; first.document.subject = QStringLiteral("one");
        auto second = first; second.uid = 2; second.document.subject = QStringLiteral("two");
        window.applySnapshot(account.id, first.folder, {{first.folder, u'/', {}, 2}}, {first, second}, {});
        QSignalSpy recent(&window, &MainWindow::refreshAllRequested), full(&window, &MainWindow::fullRefreshRequested), flags(&window, &MainWindow::messageFlagRequested), automatic(&window, &MainWindow::automaticFlagRequested);
        auto *refresh = window.findChild<QAction *>(QStringLiteral("refreshAction"));
        auto *all = window.findChild<QAction *>(QStringLiteral("fullRefreshAction"));
        QCOMPARE(refresh->text(), QStringLiteral("最新の情報を取得")); QCOMPARE(all->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_F5));
        refresh->trigger(); all->trigger(); QCOMPARE(recent.size(), 1); QCOMPARE(full.size(), 1);
        QCOMPARE(window.findChild<QAction *>(QStringLiteral("composeAction"))->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_N));
        QCOMPARE(window.findChild<QAction *>(QStringLiteral("replyAction"))->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_R));
        auto *view = window.findChild<QTreeView *>(QStringLiteral("messageList"));
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto badge = view->model()->index(0, 0); const auto rect = view->visualRect(badge);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(rect.left() + 1, rect.center().y()));
        QCOMPARE(flags.size(), 0);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QCOMPARE(flags.size(), 1); QVERIFY(window.selectedMessage()->important);
        QCOMPARE(badge.data().toString(), QStringLiteral("★"));
        const auto width = view->columnWidth(0); QVERIFY(width < 70);
        auto *autoRead = window.findChild<QAction *>(QStringLiteral("autoReadAction")); QVERIFY(!autoRead->isChecked());
        autoRead->setChecked(true);
        view->setCurrentIndex(view->model()->index(1, 3));
        QCOMPARE(flags.size(), 1); QCOMPARE(automatic.size(), 1); QVERIFY(window.selectedMessage()->seen);
        QCOMPARE(view->model()->index(1, 1).data().toString(), QStringLiteral("○"));
        view->setCurrentIndex(view->model()->index(1, 4)); QCOMPARE(flags.size(), 1); QCOMPARE(automatic.size(), 1);
        autoRead->setChecked(false); view->setCurrentIndex(view->model()->index(0, 3));
        QCOMPARE(flags.size(), 1); QVERIFY(!window.selectedMessage()->seen);
    }
    void paginationIgnoresStaleResultsAndResetsFilters()
    {
        using namespace smoothmail;
        MainWindow window(false); window.setPagedStorage(true);
        Account account; account.id = QStringLiteral("pages"); account.email = QStringLiteral("pages@example.test"); window.setAccounts({account});
        QSignalSpy requests(&window, &MainWindow::pageRequested);
        window.applySnapshot(account.id, QStringLiteral("INBOX"), {{QStringLiteral("INBOX"), u'/', {}, 0}}, {}, {});
        QCOMPARE(requests.size(), 1);
        MailPage initial; initial.query = qvariant_cast<MailQuery>(requests.last().first()); initial.total = 234;
        for (int i = 1; i <= 100; ++i) { MailRecord row{account.id, QStringLiteral("INBOX"), 10, i}; row.document.subject = QString::number(i); initial.messages.append(row); }
        window.applyPage(initial);
        auto *view = window.findChild<QTreeView *>(QStringLiteral("messageList"));
        auto *page = window.findChild<QSpinBox *>(QStringLiteral("mailPage"));
        auto *range = window.findChild<QLabel *>(QStringLiteral("pageRange"));
        QCOMPARE(view->model()->rowCount(), 100); QCOMPARE(page->maximum(), 3); QCOMPARE(range->text(), QStringLiteral("1 - 100 / 234件"));
        page->setValue(3); QCOMPARE(requests.size(), 2);
        MailPage last = initial; last.query = qvariant_cast<MailQuery>(requests.last().first()); last.messages = last.messages.mid(0, 34);
        window.applyPage(last); QCOMPARE(view->model()->rowCount(), 34); QCOMPARE(range->text(), QStringLiteral("201 - 234 / 234件"));
        window.applyPage(initial); QCOMPARE(view->model()->rowCount(), 34); QCOMPARE(page->value(), 3);
        window.findChild<QLineEdit *>(QStringLiteral("mailFilter"))->setText(QStringLiteral("日本語")); QCOMPARE(requests.size(), 2);
        window.findChild<QPushButton *>(QStringLiteral("mailSearch"))->click();
        auto query = qvariant_cast<MailQuery>(requests.last().first()); QCOMPARE(query.text, QStringLiteral("日本語")); QCOMPARE(query.page, 1);
        window.findChild<QCheckBox *>(QStringLiteral("importantOnly"))->setChecked(true);
        query = qvariant_cast<MailQuery>(requests.last().first()); QVERIFY(query.importantOnly);
        MailPage empty; empty.query = query; window.applyPage(empty);
        QCOMPARE(range->text(), QStringLiteral("0 - 0 / 0件")); QVERIFY(!page->isEnabled()); QCOMPARE(view->model()->rowCount(), 0);
    }
    void lastRemoteErrorSurvivesCacheAndRetry()
    {
        smoothmail::MainWindow window(false);
        smoothmail::Account account; account.id = QStringLiteral("error"); account.email = QStringLiteral("error@example.test");
        window.setAccounts({account});
        auto *tree = window.findChild<QTreeView *>(QStringLiteral("folderTree"));
        window.applySnapshot(account.id, {}, {}, {}, QStringLiteral("synthetic failure"));
        QVERIFY(!tree->model()->index(0, 0).data(Qt::DecorationRole).value<QIcon>().isNull());
        window.applySnapshot(account.id, {}, {}, {}, {}, false);
        QVERIFY(tree->model()->index(0, 0).data(Qt::ToolTipRole).toString().contains(QStringLiteral("synthetic failure")));
        window.setRefreshing(account.id, true); window.setRefreshing(account.id, false);
        QVERIFY(!tree->model()->index(0, 0).data(Qt::DecorationRole).value<QIcon>().isNull());
        window.applySnapshot(account.id, {}, {}, {}, {});
        QVERIFY(tree->model()->index(0, 0).data(Qt::DecorationRole).value<QIcon>().isNull());
    }
    void hasSpecifiedPanes()
    {
        smoothmail::MainWindow window(false);
        const auto *main = window.findChild<QSplitter *>(QStringLiteral("mainSplitter"));
        const auto *mail = window.findChild<QSplitter *>(QStringLiteral("mailSplitter"));
        QVERIFY(main);
        QVERIFY(mail);
        QCOMPARE(main->orientation(), Qt::Horizontal);
        QCOMPARE(main->count(), 2);
        QCOMPARE(mail->orientation(), Qt::Vertical);
        QCOMPARE(mail->count(), 2);
        const auto *folders = window.findChild<QTreeView *>(QStringLiteral("folderTree"));
        const auto *messages = window.findChild<QTreeView *>(QStringLiteral("messageList"));
        QVERIFY(folders);
        QVERIFY(messages);
        QCOMPARE(folders->model()->rowCount(), 0);
        QCOMPARE(messages->model()->rowCount(), 0);
        QCOMPARE(messages->model()->columnCount(), 7);
    }

    void unimplementedActionsAreDisabled()
    {
        smoothmail::MainWindow window(false);
        for (const auto *name : {"refreshAction", "autoRefreshAction", "composeAction",
                                 "replyAction", "addAccountAction", "downloadHistoryAction"}) {
            const auto *action = window.findChild<QAction *>(QLatin1String(name));
            QVERIFY(action);
            QVERIFY(!action->isEnabled());
        }
    }

    void bodyDoesNotNavigateAutomatically()
    {
        smoothmail::MainWindow window(false);
        const auto *body = window.findChild<QTextBrowser *>(QStringLiteral("messageBody"));
        QVERIFY(body);
        QVERIFY(!body->openLinks());
        QVERIFY(!body->openExternalLinks());
    }

    void canCloseWithoutTray()
    {
        smoothmail::MainWindow window(false);
        window.show();
        QVERIFY(window.isVisible());
        QVERIFY(window.close());
        QVERIFY(!window.isVisible());
    }
    void displaysSnapshotsAndBuildsThreads()
    {
        smoothmail::MainWindow window(false);
        smoothmail::Account account;
        account.id = QStringLiteral("a"); account.email = QStringLiteral("test@example.test");
        window.setAccounts({account});
        smoothmail::MailRecord root;
        root.accountId = account.id; root.folder = QStringLiteral("INBOX"); root.uidValidity = 1; root.uid = 1;
        root.document.messageId = "root@test"; root.document.subject = QStringLiteral("root");
        root.document.parts.append({QStringLiteral("text/plain"), QStringLiteral("safe fixture body")});
        auto child = root;
        child.uid = 2; child.document.messageId = "child@test"; child.document.inReplyTo = {"root@test"};
        window.applySnapshot(account.id, root.folder,
            {{root.folder, u'/', {}, 2}, {QStringLiteral("Parent"), u'/', {}, 1}, {QStringLiteral("Parent/Child"), u'/', {}, 2}}, {root, child}, {});
        auto *folders = window.findChild<QTreeView *>(QStringLiteral("folderTree"));
        QCOMPARE(folders->model()->index(0, 0).data().toString(), QStringLiteral("test@example.test (5)"));
        auto *messages = window.findChild<QTreeView *>(QStringLiteral("messageList"));
        QCOMPARE(messages->model()->rowCount(), 2);
        window.findChild<QAction *>(QStringLiteral("threadAction"))->setChecked(true);
        QCOMPARE(messages->model()->rowCount(), 1);
        QCOMPARE(messages->treePosition(), 3);
        QCOMPARE(messages->model()->rowCount(messages->model()->index(0, 0)), 1);
        const auto childIndex = messages->model()->index(0, 3, messages->model()->index(0, 0));
        QVERIFY(childIndex.isValid());
        window.findChild<QAction *>(QStringLiteral("autoReadAction"))->setChecked(true);
        messages->setCurrentIndex(childIndex);
        QCOMPARE(childIndex.siblingAtColumn(1).data().toString(), QStringLiteral("○"));
        QVERIFY(window.selectedMessage()->seen);
        messages->setCurrentIndex(messages->model()->index(0, 0));
        auto *body = window.findChild<QTextBrowser *>(QStringLiteral("messageBody"));
        QVERIFY(body->toPlainText().contains(QStringLiteral("safe fixture body")));
        window.applySnapshot(account.id, root.folder, {}, {}, QStringLiteral("offline"));
        QCOMPARE(messages->model()->rowCount(), 1); // Failure preserves readable mail.
    }
    void sortOrderTogglesAndSurvivesRefresh()
    {
        smoothmail::MainWindow window(false);
        smoothmail::Account account; account.id = QStringLiteral("sort"); account.email = QStringLiteral("sort@example.test");
        window.setAccounts({account});
        smoothmail::MailRecord first{account.id, QStringLiteral("INBOX"), 1, 1};
        first.document.subject = QStringLiteral("Alpha");
        auto second = first; second.uid = 2; second.document.subject = QStringLiteral("Zulu");
        const QList<smoothmail::MailFolder> folders{{first.folder, u'/', {}, 0}};
        window.applySnapshot(account.id, first.folder, folders, {first, second}, {});
        auto *view = window.findChild<QTreeView *>(QStringLiteral("messageList"));
        QMetaObject::invokeMethod(view->header(), "sectionClicked", Q_ARG(int, 3));
        QCOMPARE(view->model()->index(0, 3).data().toString(), QStringLiteral("Alpha"));
        QMetaObject::invokeMethod(view->header(), "sectionClicked", Q_ARG(int, 3));
        QCOMPARE(view->model()->index(0, 3).data().toString(), QStringLiteral("Zulu"));
        window.applySnapshot(account.id, first.folder, folders, {first, second}, {});
        QCOMPARE(view->model()->index(0, 3).data().toString(), QStringLiteral("Zulu"));
        QMetaObject::invokeMethod(view->header(), "sectionClicked", Q_ARG(int, 0));
        QCOMPARE(view->header()->sortIndicatorSection(), 3);
        QCOMPARE(view->header()->sortIndicatorOrder(), Qt::DescendingOrder);
    }
    void reordersOnlyAccounts()
    {
        smoothmail::FolderModel model;
        for (const auto &id : {QStringLiteral("first"), QStringLiteral("second")}) {
            auto *item = new QStandardItem(id); item->setData(id, Qt::UserRole); model.appendRow(item);
        }
        QSignalSpy order(&model, &smoothmail::FolderModel::accountOrderChanged);
        std::unique_ptr<QMimeData> data(model.mimeData({model.index(1, 0)}));
        QVERIFY(!model.dropMimeData(data.get(), Qt::CopyAction, 0, 0, model.index(0, 0)));
        QVERIFY(model.dropMimeData(data.get(), Qt::CopyAction, 0, 0, {}));
        QCOMPARE(order.size(), 1);
        QCOMPARE(order.first().first().toStringList(), QStringList({QStringLiteral("second"), QStringLiteral("first")}));
        QCOMPARE(model.rowCount(), 2);
    }
};

QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
