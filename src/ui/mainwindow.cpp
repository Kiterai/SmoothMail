#include "mainwindow.h"

#include "messageviewer.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTreeView>

namespace smoothmail {
namespace {

QIcon applicationIcon()
{
    // Original envelope icon drawn at runtime; no third-party image assets.
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(45, 65, 90), 2));
    painter.setBrush(QColor(235, 240, 245));
    painter.drawRoundedRect(QRectF(3, 7, 26, 19), 2, 2);
    painter.drawLine(QPointF(4, 9), QPointF(16, 18));
    painter.drawLine(QPointF(16, 18), QPointF(28, 9));
    return QIcon(pixmap);
}

QAction *pendingAction(QMenu *menu, const QString &text, const char *name)
{
    auto *action = menu->addAction(text);
    action->setObjectName(QLatin1String(name));
    action->setEnabled(false);
    action->setStatusTip(QStringLiteral("この機能は未実装です。"));
    return action;
}

} // namespace

MainWindow::MainWindow(bool enableSystemTray, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("SmoothMail"));
    setWindowIcon(applicationIcon());
    resize(1100, 760);
    createMenus();
    createPanes();
    statusBar()->addWidget(new QLabel(QStringLiteral("最終更新: 未更新"), this));
    statusBar()->showMessage(QStringLiteral("開発中: メールサーバ接続は未実装です。"));
    if (enableSystemTray && QSystemTrayIcon::isSystemTrayAvailable()) {
        createTray();
    }
}

void MainWindow::createMenus()
{
    auto *edit = menuBar()->addMenu(QStringLiteral("編集"));
    pendingAction(edit, QStringLiteral("元に戻す"), "undoAction");
    pendingAction(edit, QStringLiteral("やり直す"), "redoAction");

    auto *view = menuBar()->addMenu(QStringLiteral("表示"));
    auto *threads = pendingAction(view, QStringLiteral("スレッド表示"), "threadAction");
    threads->setCheckable(true);

    auto *mail = menuBar()->addMenu(QStringLiteral("メール"));
    auto *refresh = pendingAction(mail, QStringLiteral("最新の情報に更新"), "refreshAction");
    refresh->setShortcut(QKeySequence(Qt::Key_F5));
    auto *automatic = pendingAction(mail, QStringLiteral("自動で更新"), "autoRefreshAction");
    automatic->setCheckable(true);
    pendingAction(mail, QStringLiteral("新規メール"), "composeAction");
    pendingAction(mail, QStringLiteral("新規返信メール"), "replyAction");

    auto *tools = menuBar()->addMenu(QStringLiteral("ツール"));
    pendingAction(tools, QStringLiteral("ダウンロード履歴"), "downloadHistoryAction");
    auto *settings = menuBar()->addMenu(QStringLiteral("設定"));
    auto *addAccount = pendingAction(settings, QStringLiteral("アカウントの追加"), "addAccountAction");
    connect(addAccount, &QAction::triggered, this, &MainWindow::addAccountRequested);
}

void MainWindow::createPanes()
{
    auto *horizontal = new QSplitter(Qt::Horizontal, this);
    horizontal->setObjectName(QStringLiteral("mainSplitter"));
    auto *folders = new QTreeView(horizontal);
    folders->setObjectName(QStringLiteral("folderTree"));
    auto *folderModel = new QStandardItemModel(folders);
    m_folders = folderModel;
    folderModel->setHorizontalHeaderLabels({QStringLiteral("アカウント / フォルダ")});
    folders->setModel(folderModel);

    auto *vertical = new QSplitter(Qt::Vertical, horizontal);
    vertical->setObjectName(QStringLiteral("mailSplitter"));
    auto *messages = new QTreeView(vertical);
    messages->setObjectName(QStringLiteral("messageList"));
    auto *messageModel = new QStandardItemModel(messages);
    messageModel->setHorizontalHeaderLabels({
        QStringLiteral("重要"), QStringLiteral("未読"), QStringLiteral("添付"),
        QStringLiteral("メールタイトル"), QStringLiteral("差出人名"),
        QStringLiteral("差出人アドレス"), QStringLiteral("日時")});
    messages->setModel(messageModel);
    messages->setUniformRowHeights(true);
    messages->header()->setStretchLastSection(true);

    auto *viewer = new MessageViewer(vertical);
    viewer->setObjectName(QStringLiteral("messageViewer"));
    connect(viewer, &MessageViewer::externalLinkRequested, this, [](const QUrl &url) {
        QDesktopServices::openUrl(url);
    });
    horizontal->setSizes({250, 850});
    vertical->setSizes({300, 400});
    horizontal->setStretchFactor(1, 1);
    vertical->setStretchFactor(1, 1);
    setCentralWidget(horizontal);
}

void MainWindow::enableAccountManagement(bool enabled)
{
    auto *action = findChild<QAction *>(QStringLiteral("addAccountAction"));
    action->setEnabled(enabled);
    action->setStatusTip(enabled ? QStringLiteral("アカウントを追加します。") : QStringLiteral("保存先を利用できません。"));
}

void MainWindow::setAccounts(const QList<Account> &accounts)
{
    m_folders->removeRows(0, m_folders->rowCount());
    for (const auto &account : accounts) {
        auto *item = new QStandardItem(account.email);
        item->setEditable(false);
        item->setData(account.id, Qt::UserRole);
        m_folders->appendRow(item);
    }
    statusBar()->showMessage(accounts.isEmpty() ? QStringLiteral("アカウントを追加してください。")
                                              : QStringLiteral("アカウント設定を読み込みました。メール同期は準備中です。"));
}

void MainWindow::createTray()
{
    m_tray = new QSystemTrayIcon(windowIcon(), this);
    m_tray->setToolTip(QStringLiteral("SmoothMail"));
    auto *menu = new QMenu(this);
    menu->addAction(QStringLiteral("表示"), this, &MainWindow::restoreWindow);
    menu->addAction(QStringLiteral("終了"), this, [this] {
        m_exiting = true;
        m_tray->hide();
        close();
        QApplication::quit();
    });
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    restoreWindow();
                }
            });
    m_tray->show();
}

void MainWindow::restoreWindow()
{
    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_exiting && m_tray && m_tray->isVisible()
        && QSystemTrayIcon::isSystemTrayAvailable()) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

} // namespace smoothmail
