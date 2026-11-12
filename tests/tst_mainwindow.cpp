#include "ui/mainwindow.h"

#include <QAction>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeView>
#include <QtTest>

class MainWindowTest final : public QObject
{
    Q_OBJECT

private slots:
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
};

QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
