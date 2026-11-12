#include "ui/mainwindow.h"
#include "ui/foldermodel.h"

#include <QAction>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeView>
#include <QtTest>
#include <QMimeData>
#include <QHeaderView>

class MainWindowTest final : public QObject
{
    Q_OBJECT

private slots:
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
        QCOMPARE(messages->model()->rowCount(messages->model()->index(0, 0)), 1);
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
