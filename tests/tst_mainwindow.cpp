#include "ui/mainwindow.h"
#include "ui/foldermodel.h"
#include "ui/composedialog.h"
#include "ui/visiblesplitter.h"

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
#include <QMenu>
#include <QTimer>

class MainWindowTest final : public QObject
{
    Q_OBJECT

private slots:
    void splitterLinesContrastAndRemainDraggable_data()
    {
        QTest::addColumn<bool>("dark"); QTest::addColumn<bool>("vertical");
        for (bool dark : {false, true}) for (bool vertical : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(dark ? "dark" : "light", vertical ? "vertical" : "horizontal"))) << dark << vertical;
    }
    void splitterLinesContrastAndRemainDraggable()
    {
        QFETCH(bool, dark); QFETCH(bool, vertical);
        smoothmail::VisibleSplitter splitter(vertical ? Qt::Vertical : Qt::Horizontal);
        QPalette palette = splitter.palette(); palette.setColor(QPalette::Window, dark ? Qt::black : Qt::white); splitter.setPalette(palette);
        splitter.addWidget(new QWidget); splitter.addWidget(new QWidget); splitter.resize(500, 500); splitter.setSizes({240, 240});
        splitter.show(); QVERIFY(QTest::qWaitForWindowExposed(&splitter)); auto *handle = splitter.handle(1);
        QVERIFY(handle->isVisible()); const auto image = handle->grab().toImage();
        const auto point = handle->rect().center(); const auto ratio = image.devicePixelRatio();
        const auto pixel = image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
        QCOMPARE(pixel, QColor(dark ? Qt::white : Qt::black));
        const auto beside = point - (vertical ? QPoint(0, 1) : QPoint(1, 0));
        QCOMPARE(image.pixelColor(qRound(beside.x() * ratio), qRound(beside.y() * ratio)), QColor(dark ? Qt::black : Qt::white));
        const auto before = splitter.sizes().first();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(handle, point + (vertical ? QPoint(0, 30) : QPoint(30, 0)));
        QTest::mouseRelease(handle, Qt::LeftButton);
        QTRY_VERIFY(splitter.sizes().first() != before);
        smoothmail::MainWindow window(false);
        for (auto *pane : window.findChildren<QSplitter *>()) QVERIFY(dynamic_cast<smoothmail::VisibleSplitter *>(pane));
    }
    void replySourceIsLiteralClickableLabelAndKeepsDraft()
    {
        using namespace smoothmail; ComposeData data; data.replySubject = QStringLiteral("<img src='https://tracker.example/'>");
        data.subject = QStringLiteral("reply"); data.body = QStringLiteral("draft");
        ComposeDialog dialog(data, nullptr, true); dialog.show(); QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *source = dialog.findChild<QLabel *>(QStringLiteral("composeReplySource")); QVERIFY(source);
        QCOMPARE(source->textFormat(), Qt::PlainText); QCOMPARE(source->text(), data.replySubject);
        QVERIFY(source->styleSheet().contains(QStringLiteral(":hover"))); QSignalSpy requested(&dialog, &ComposeDialog::replySourceRequested);
        QTest::mouseClick(source, Qt::LeftButton); QCOMPARE(requested.size(), 1);
        QTest::mouseClick(source, Qt::RightButton); QCOMPARE(requested.size(), 1);
        source->setFocus(); QTest::keyClick(source, Qt::Key_Return); QCOMPARE(requested.size(), 2);
        dialog.sourceNavigationFailed(QStringLiteral("fixture missing"));
        QCOMPARE(dialog.draft().body, data.body); QCOMPARE(dialog.draft().subject, data.subject);
        ComposeDialog empty({}, nullptr, false); auto *blank = empty.findChild<QLabel *>(QStringLiteral("composeReplySource"));
        QVERIFY(blank->text().isEmpty()); QSignalSpy absent(&empty, &ComposeDialog::replySourceRequested);
        QTest::mouseClick(blank, Qt::LeftButton); QVERIFY(absent.isEmpty());
        ComposeDialog untitled({}, nullptr, true); QSignalSpy present(&untitled, &ComposeDialog::replySourceRequested);
        QTest::mouseClick(untitled.findChild<QLabel *>(QStringLiteral("composeReplySource")), Qt::LeftButton); QCOMPARE(present.size(), 1);
    }
    void contextRefreshTargetsClickedItemWithoutChangingGlobalActions()
    {
        using namespace smoothmail; MainWindow window(false); Account account; account.id = QStringLiteral("target"); account.email = QStringLiteral("target@example.test"); window.setAccounts({account});
        window.applySnapshot(account.id, QStringLiteral("INBOX"), {{QStringLiteral("INBOX"), u'/', {}, 0}, {QStringLiteral("Archive"), u'/', {}, 0}}, {}, {});
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window)); auto *tree = window.findChild<QTreeView *>(QStringLiteral("folderTree")); tree->expandAll();
        QSignalSpy target(&window, &MainWindow::targetRefreshRequested), all(&window, &MainWindow::refreshAllRequested), full(&window, &MainWindow::fullRefreshRequested);
        const auto accountIndex = tree->model()->index(0, 0); const auto folders = tree->model()->match(accountIndex, Qt::UserRole + 1, QStringLiteral("Archive"), 1, Qt::MatchExactly | Qt::MatchRecursive); QCOMPARE(folders.size(), 1);
        const auto invoke = [&](const QModelIndex &index, bool complete) {
            QTimer::singleShot(20, &window, [complete] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); if (!menu) return;
                for (auto *action : menu->actions()) if (action->objectName() == (complete ? QStringLiteral("targetFullRefresh") : QStringLiteral("targetRecentRefresh"))) { menu->setActiveAction(action); QTest::keyClick(menu, Qt::Key_Return); return; }
                menu->close();
            });
            emit tree->customContextMenuRequested(tree->visualRect(index).center());
        };
        invoke(folders.first(), false); QCOMPARE(target.size(), 1); QCOMPARE(target.last().at(0).toString(), account.id); QCOMPARE(target.last().at(1).toString(), QStringLiteral("Archive")); QVERIFY(!target.last().at(2).toBool());
        invoke(accountIndex, true); QCOMPARE(target.size(), 2); QVERIFY(target.last().at(1).toString().isEmpty()); QVERIFY(target.last().at(2).toBool()); QVERIFY(all.isEmpty()); QVERIFY(full.isEmpty());
    }
    void bodyLoadingFailureAndStaleRepliesAreDistinct()
    {
        using namespace smoothmail; MainWindow window(false); window.setPagedStorage(true);
        Account account; account.id = QStringLiteral("body-state"); account.email = QStringLiteral("body@example.test"); window.setAccounts({account});
        QSignalSpy pages(&window, &MainWindow::pageRequested);
        window.applySnapshot(account.id, QStringLiteral("INBOX"), {{QStringLiteral("INBOX"), u'/', {}, 0}}, {}, {}); QVERIFY(!pages.isEmpty());
        MailPage page; page.query = qvariant_cast<MailQuery>(pages.last().first()); page.total = 2;
        page.messages = {{account.id, QStringLiteral("INBOX"), 10, 1}, {account.id, QStringLiteral("INBOX"), 10, 2}}; window.applyPage(page);
        auto *view = window.findChild<QTreeView *>(QStringLiteral("messageList")); auto *body = window.findChild<QTextBrowser *>(QStringLiteral("messageBody"));
        view->setCurrentIndex(view->model()->index(0, 3)); QCOMPARE(body->toPlainText(), QStringLiteral("読み込み中..."));
        window.displayLoadedMessage(page.messages[1], QStringLiteral("stale")); QCOMPARE(body->toPlainText(), QStringLiteral("読み込み中..."));
        window.displayLoadedMessage(page.messages[0], QStringLiteral("fixture error")); QCOMPARE(body->toPlainText(), QStringLiteral("読み込みに失敗しました"));
        auto first = page.messages[0]; first.document.parts = {{QStringLiteral("text/plain"), QStringLiteral("first body")}}; window.displayLoadedMessage(first, {}); QCOMPARE(body->toPlainText(), QStringLiteral("first body"));
        view->setCurrentIndex(view->model()->index(1, 3)); QCOMPARE(body->toPlainText(), QStringLiteral("読み込み中..."));
        window.displayLoadedMessage(first, {}); QCOMPARE(body->toPlainText(), QStringLiteral("読み込み中..."));
    }
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
        auto *parallel = dialog.findChild<QSpinBox *>(QStringLiteral("settingsParallelUpdates"));
        auto *buttons = dialog.findChild<QDialogButtonBox *>(); auto *button = buttons->button(QDialogButtonBox::Save);
        QCOMPARE(page->value(), 100); QCOMPARE(interval->value(), 30); QVERIFY(!button->isEnabled());
        QCOMPARE(parallel->value(), 4); parallel->setValue(2); QVERIFY(button->isEnabled());
        button->click(); QCOMPARE(qvariant_cast<SystemSettings>(save.last().first()).parallelUpdates, 2); QVERIFY(!parallel->isEnabled());
        dialog.saved(QStringLiteral("fixture failure")); QVERIFY(parallel->isEnabled()); QCOMPARE(parallel->value(), 2);
        parallel->setValue(4); save.clear(); QVERIFY(!button->isEnabled());
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
