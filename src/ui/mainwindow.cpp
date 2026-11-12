#include "mainwindow.h"

#include "messageviewer.h"
#include "core/threading.h"
#include "foldermodel.h"

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
#include <QTimer>
#include <QItemSelectionModel>
#include <QStyle>
#include <QScreen>
#include <algorithm>
#include <functional>

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
    action->setStatusTip(text);
    return action;
}

} // namespace

MainWindow::MainWindow(bool enableSystemTray, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("SmoothMail"));
    setWindowIcon(applicationIcon());
    const auto available = screen()->availableGeometry().size();
    resize(qMin(1100, available.width() - 80), qMin(760, available.height() - 80));
    createMenus();
    createPanes();
    m_lastUpdated = new QLabel(QStringLiteral("最終更新: 未更新"), this);
    statusBar()->addWidget(m_lastUpdated);
    auto *clock = new QTimer(this);
    clock->start(1000);
    connect(clock, &QTimer::timeout, this, [this] {
        if (!m_lastSuccess.isValid()) return;
        const auto seconds = qMax<qint64>(0, m_lastSuccess.secsTo(QDateTime::currentDateTime()));
        const auto ago = seconds < 60 ? QStringLiteral("<1分前") : seconds < 3600
            ? QStringLiteral("%1分前").arg(seconds / 60) : QStringLiteral("%1時間前").arg(seconds / 3600);
        m_lastUpdated->setText(QStringLiteral("最終更新: %1 (%2)").arg(m_lastSuccess.toString(QStringLiteral("yyyy/MM/dd HH:mm:ss")), ago));
    });
    statusBar()->showMessage(QStringLiteral("アカウント設定を読み込み中..."));
    if (enableSystemTray && QSystemTrayIcon::isSystemTrayAvailable()) {
        createTray();
    }
}

void MainWindow::createMenus()
{
    auto *edit = menuBar()->addMenu(QStringLiteral("編集"));
    connect(pendingAction(edit, QStringLiteral("元に戻す"), "undoAction"), &QAction::triggered,
        this, [this] { emit undoRedoRequested(false); });
    connect(pendingAction(edit, QStringLiteral("やり直す"), "redoAction"), &QAction::triggered,
        this, [this] { emit undoRedoRequested(true); });

    auto *view = menuBar()->addMenu(QStringLiteral("表示"));
    auto *threads = pendingAction(view, QStringLiteral("スレッド表示"), "threadAction");
    threads->setCheckable(true);
    connect(threads, &QAction::toggled, this, [this] { rebuildMessages(); });

    auto *mail = menuBar()->addMenu(QStringLiteral("メール"));
    auto *refresh = pendingAction(mail, QStringLiteral("最新の情報に更新"), "refreshAction");
    refresh->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refresh, &QAction::triggered, this, &MainWindow::refreshAllRequested);
    auto *automatic = pendingAction(mail, QStringLiteral("自動で更新"), "autoRefreshAction");
    automatic->setCheckable(true);
    auto *poll = new QTimer(this);
    poll->setInterval(30000);
    connect(automatic, &QAction::toggled, this, [poll](bool enabled) { if (enabled) poll->start(); else poll->stop(); });
    connect(poll, &QTimer::timeout, this, &MainWindow::refreshAllRequested);
    connect(pendingAction(mail, QStringLiteral("新規メール"), "composeAction"), &QAction::triggered,
            this, [this] { emit composeRequested(false); });
    connect(pendingAction(mail, QStringLiteral("新規返信メール"), "replyAction"), &QAction::triggered,
            this, [this] { emit composeRequested(true); });

    auto *tools = menuBar()->addMenu(QStringLiteral("ツール"));
    connect(pendingAction(tools, QStringLiteral("ダウンロード履歴"), "downloadHistoryAction"), &QAction::triggered,
        this, &MainWindow::historyRequested);
    auto *settings = menuBar()->addMenu(QStringLiteral("設定"));
    auto *addAccount = pendingAction(settings, QStringLiteral("アカウントの追加"), "addAccountAction");
    connect(addAccount, &QAction::triggered, this, &MainWindow::addAccountRequested);
}

void MainWindow::createPanes()
{
    auto *horizontal = new QSplitter(Qt::Horizontal, this);
    horizontal->setObjectName(QStringLiteral("mainSplitter"));
    auto *folders = new QTreeView(horizontal);
    m_folderView = folders;
    folders->setObjectName(QStringLiteral("folderTree"));
    auto *folderModel = new FolderModel(folders);
    m_folders = folderModel;
    folderModel->setHorizontalHeaderLabels({QStringLiteral("アカウント / フォルダ")});
    folders->setModel(folderModel);
    folders->setDragEnabled(true); folders->setAcceptDrops(true); folders->setDropIndicatorShown(true);
    folders->setDragDropMode(QAbstractItemView::DragDrop);
    folders->setDefaultDropAction(Qt::CopyAction);
    connect(folderModel, &FolderModel::accountOrderChanged, this, &MainWindow::accountOrderChanged);
    folders->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(folders, &QTreeView::customContextMenuRequested, this, [this, folders](const QPoint &position) {
        const auto index = folders->indexAt(position);
        if (!index.isValid() || index.parent().isValid()) return;
        const auto id = index.data(Qt::UserRole).toString();
        Account account;
        for (const auto &candidate : m_accounts) if (candidate.id == id) account = candidate;
        QMenu menu(this);
        auto *smtp = menu.addAction(QStringLiteral("SMTPのOAuthを再認可"));
        auto *imap = menu.addAction(QStringLiteral("IMAP4のOAuthを再認可"));
        smtp->setEnabled(account.smtp.authentication == AuthenticationMode::OAuth2);
        imap->setEnabled(account.imap.authentication == AuthenticationMode::OAuth2);
        const auto *chosen = menu.exec(folders->viewport()->mapToGlobal(position));
        if (chosen == smtp) emit reauthorizeRequested(id, QStringLiteral("smtp"));
        if (chosen == imap) emit reauthorizeRequested(id, QStringLiteral("imap"));
    });
    connect(folders->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &index) {
        if (m_rebuilding || !index.isValid()) return;
        const auto id = index.data(Qt::UserRole).toString();
        const auto name = index.data(Qt::UserRole + 1).toString();
        if (id.isEmpty()) return;
        m_accountId = id;
        m_folder = name;
        m_revealUid = 0; m_revealValidity = 0;
        findChild<QAction *>(QStringLiteral("composeAction"))->setEnabled(!name.isEmpty());
        rebuildMessages();
        emit refreshRequested(id, name);
    });

    auto *vertical = new QSplitter(Qt::Vertical, horizontal);
    vertical->setObjectName(QStringLiteral("mailSplitter"));
    auto *messages = new QTreeView(vertical);
    m_messageView = messages;
    messages->setObjectName(QStringLiteral("messageList"));
    auto *messageModel = new QStandardItemModel(messages);
    m_messageModel = messageModel;
    messageModel->setHorizontalHeaderLabels({
        QStringLiteral("重要"), QStringLiteral("未読"), QStringLiteral("添付"),
        QStringLiteral("メールタイトル"), QStringLiteral("差出人名"),
        QStringLiteral("差出人アドレス"), QStringLiteral("日時")});
    messages->setModel(messageModel);
    messages->setUniformRowHeights(true);
    messages->header()->setStretchLastSection(true);
    messages->header()->setSectionsClickable(true);
    messages->header()->setSortIndicator(m_sortColumn, m_sortOrder);
    messages->header()->setSortIndicatorShown(true);
    messageModel->setSortRole(Qt::UserRole + 10);
    connect(messages->header(), &QHeaderView::sectionClicked, this, [this, messages, messageModel](int section) {
        auto *header = messages->header();
        // QHeaderView may already have toggled its indicator before this
        // signal. Keep independent state instead of toggling that state twice.
        if (section >= 3) {
            m_sortOrder = m_sortColumn == section && m_sortOrder == Qt::AscendingOrder
                ? Qt::DescendingOrder : Qt::AscendingOrder;
            m_sortColumn = section;
            messageModel->sort(m_sortColumn, m_sortOrder);
        }
        header->setSortIndicator(m_sortColumn, m_sortOrder);
    });

    auto *viewer = new MessageViewer(vertical);
    viewer->setObjectName(QStringLiteral("messageViewer"));
    connect(viewer, &MessageViewer::externalLinkRequested, this, [](const QUrl &url) {
        QDesktopServices::openUrl(url);
    });
    connect(viewer, &MessageViewer::attachmentRequested, this, [this](const QString &partId) {
        const auto message = selectedMessage();
        if (message) emit downloadRequested(*message, partId);
    });
    connect(messages->selectionModel(), &QItemSelectionModel::currentChanged, this, [this, viewer](const QModelIndex &index) {
        const int record = index.siblingAtColumn(0).data(Qt::UserRole).toInt();
        findChild<QAction *>(QStringLiteral("replyAction"))->setEnabled(index.isValid());
        if (!index.isValid() || record < 0 || record >= m_visibleMessages.size()) { viewer->clear(); return; }
        viewer->displayMessage(m_visibleMessages.at(record).document);
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
    m_accounts = accounts;
    rebuildFolders();
    for (const auto *name : {"refreshAction", "autoRefreshAction", "threadAction"}) {
        findChild<QAction *>(QLatin1String(name))->setEnabled(!accounts.isEmpty());
    }
    statusBar()->showMessage(accounts.isEmpty() ? QStringLiteral("アカウントを追加してください。")
                                              : QStringLiteral("アカウント設定を読み込みました。"));
}

void MainWindow::setRefreshing(const QString &id, bool refreshing)
{
    if (refreshing) m_refreshing.insert(id); else m_refreshing.remove(id);
    rebuildFolders();
    if (refreshing) {
        for (const auto &account : m_accounts) if (account.id == id) statusBar()->showMessage(account.email + QStringLiteral(" のデータを取得中..."));
    }
}

void MainWindow::applySnapshot(const QString &id, const QString &folder, const QList<MailFolder> &folders,
                               const QList<MailRecord> &messages, const QString &error, bool remoteUpdate)
{
    if (!error.isEmpty()) { statusBar()->showMessage(QStringLiteral("更新に失敗しました: ") + error); return; }
    m_folderCache[id] = folders;
    QStringList present;
    for (const auto &item : folders) present.append(item.name);
    for (const auto &cached : m_mailCache[id].keys()) if (!present.contains(cached)) m_mailCache[id].remove(cached);
    m_mailCache[id][folder] = messages;
    const bool visible = m_accountId.isEmpty() || (m_accountId == id && (m_folder.isEmpty() || m_folder == folder));
    if (visible) { m_accountId = id; m_folder = folder; }
    findChild<QAction *>(QStringLiteral("composeAction"))->setEnabled(!m_accountId.isEmpty() && !m_folder.isEmpty());
    if (remoteUpdate) m_lastSuccess = QDateTime::currentDateTime();
    rebuildFolders();
    if (visible) rebuildMessages();
    if (visible && remoteUpdate && m_revealUid > 0) {
        m_revealUid = 0;
        statusBar()->showMessage(QStringLiteral("元メッセージが見つからないか、UIDVALIDITYが変更されています。"));
        return;
    }
    statusBar()->showMessage(remoteUpdate ? QStringLiteral("更新しました。") : QStringLiteral("保存済みメールを表示しています。"));
}

std::optional<MailRecord> MainWindow::selectedMessage() const
{
    const auto index = m_messageView->currentIndex();
    const int record = index.siblingAtColumn(0).data(Qt::UserRole).toInt();
    if (!index.isValid() || record < 0 || record >= m_visibleMessages.size()) return std::nullopt;
    return m_visibleMessages.at(record);
}

void MainWindow::rebuildFolders()
{
    m_rebuilding = true;
    m_folders->removeRows(0, m_folders->rowCount());
    QModelIndex selected;
    for (const auto &account : m_accounts) {
        auto *root = new QStandardItem(account.email);
        root->setEditable(false);
        root->setDragEnabled(true); root->setDropEnabled(false);
        root->setData(account.id, Qt::UserRole);
        if (m_refreshing.contains(account.id)) root->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        m_folders->appendRow(root);
        QHash<QString, QStandardItem *> items;
        QList<MailFolder> folders = m_folderCache.value(account.id);
        std::sort(folders.begin(), folders.end(), [](const MailFolder &a, const MailFolder &b) { return a.name.size() < b.name.size(); });
        for (const auto &folder : folders) {
            QStandardItem *parent = root;
            QString path;
            const auto parts = folder.separator.isNull() ? QStringList{folder.name} : folder.name.split(folder.separator);
            for (const auto &part : parts) {
                if (!path.isEmpty()) path += folder.separator;
                path += part;
                if (!items.contains(path)) {
                    auto *node = new QStandardItem(part);
                    node->setEditable(false);
                    node->setDragEnabled(false); node->setDropEnabled(false);
                    node->setData(account.id, Qt::UserRole);
                    node->setData(path, Qt::UserRole + 1);
                    parent->appendRow(node);
                    items[path] = node;
                }
                parent = items.value(path);
            }
            parent->setData(folder.unread, Qt::UserRole + 2);
            for (const auto &flag : folder.flags) if (flag.toUpper() == "\\NOSELECT") parent->setSelectable(false);
            if (account.id == m_accountId && folder.name == m_folder) selected = parent->index();
        }
        const std::function<qint64(QStandardItem *)> decorate = [&](QStandardItem *item) {
            qint64 unread = item->data(Qt::UserRole + 2).toLongLong();
            for (int i = 0; i < item->rowCount(); ++i) unread += decorate(item->child(i));
            if (unread > 0) item->setText(item->text() + QStringLiteral(" (%1)").arg(unread));
            auto font = item->font(); font.setBold(unread > 0); item->setFont(font);
            return unread;
        };
        decorate(root);
    }
    m_folderView->expandAll();
    if (selected.isValid()) m_folderView->setCurrentIndex(selected);
    m_rebuilding = false;
}

void MainWindow::rebuildMessages()
{
    if (!m_messageModel) return;
    qint64 previousUid = 0, previousValidity = 0;
    QString previousAccount, previousFolder;
    const auto previous = m_messageView->currentIndex();
    const auto previousRow = previous.siblingAtColumn(0).data(Qt::UserRole).toInt();
    if (previous.isValid() && previousRow >= 0 && previousRow < m_visibleMessages.size()) {
        previousUid = m_visibleMessages.at(previousRow).uid;
        previousValidity = m_visibleMessages.at(previousRow).uidValidity;
        previousAccount = m_visibleMessages.at(previousRow).accountId;
        previousFolder = m_visibleMessages.at(previousRow).folder;
    }
    m_messageModel->removeRows(0, m_messageModel->rowCount());
    findChild<MessageViewer *>()->clear();
    m_visibleMessages = m_mailCache.value(m_accountId).value(m_folder);
    QList<ThreadMessage> threads;
    for (const auto &record : m_visibleMessages) {
        const auto &doc = record.document;
        threads.append({record.accountId, record.folder + u'/' + QString::number(record.uidValidity) + u'/' + QString::number(record.uid), doc.messageId, doc.inReplyTo, doc.references});
    }
    const auto accountCache = m_mailCache.value(m_accountId);
    for (auto it = accountCache.cbegin(); it != accountCache.cend(); ++it) {
        if (it.key() == m_folder) continue;
        for (const auto &record : it.value()) {
            const auto &doc = record.document;
            threads.append({record.accountId, record.folder + u'/' + QString::number(record.uidValidity) + u'/' + QString::number(record.uid), doc.messageId, doc.inReplyTo, doc.references});
        }
    }
    QList<int> parents(m_visibleMessages.size(), -1);
    if (findChild<QAction *>(QStringLiteral("threadAction"))->isChecked()) {
        const auto allParents = threadParents(threads);
        for (qsizetype i = 0; i < parents.size(); ++i) {
            int parent = allParents.at(i);
            while (parent >= parents.size()) parent = allParents.at(parent);
            parents[i] = parent;
        }
        parents = compactThreadParents(parents);
    }
    QList<QList<QStandardItem *>> rows;
    for (qsizetype i = 0; i < m_visibleMessages.size(); ++i) {
        const auto &record = m_visibleMessages.at(i);
        const auto &doc = record.document;
        const QStringList cells{record.important ? QStringLiteral("★") : QString{}, record.seen ? QString{} : QStringLiteral("●"),
            doc.attachments.isEmpty() ? QString{} : QStringLiteral("●"), doc.subject, doc.senderName, doc.senderAddress,
            doc.date.toLocalTime().toString(QStringLiteral("yyyy/MM/dd HH:mm"))};
        QList<QStandardItem *> row;
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QStandardItem(cells.at(c));
            item->setEditable(false);
            item->setData(c == 6 ? QVariant(doc.date.toMSecsSinceEpoch()) : QVariant(cells.at(c)), Qt::UserRole + 10);
            auto font = item->font(); font.setBold(!record.seen); item->setFont(font);
            row.append(item);
        }
        row.first()->setData(i, Qt::UserRole);
        rows.append(row);
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (parents.at(i) >= 0) rows[parents.at(i)].first()->appendRow(rows.at(i));
        else m_messageModel->appendRow(rows.at(i));
    }
    m_messageModel->sort(m_sortColumn, m_sortOrder);
    if (m_revealUid > 0) { previousUid = m_revealUid; previousValidity = m_revealValidity; previousAccount = m_accountId; previousFolder = m_folder; }
    for (qsizetype i = 0; i < m_visibleMessages.size(); ++i) {
        if (m_visibleMessages.at(i).uid == previousUid && m_visibleMessages.at(i).uidValidity == previousValidity
            && m_visibleMessages.at(i).accountId == previousAccount && m_visibleMessages.at(i).folder == previousFolder) {
            m_messageView->setCurrentIndex(rows.at(i).first()->index());
            m_revealUid = 0;
            break;
        }
    }
    for (int c = 0; c < 6; ++c) m_messageView->resizeColumnToContents(c);
}

void MainWindow::setEditingState(bool undoAvailable, bool redoAvailable)
{
    findChild<QAction *>(QStringLiteral("undoAction"))->setEnabled(undoAvailable);
    findChild<QAction *>(QStringLiteral("redoAction"))->setEnabled(redoAvailable);
}

void MainWindow::enableHistory(bool enabled)
{
    findChild<QAction *>(QStringLiteral("downloadHistoryAction"))->setEnabled(enabled);
}

void MainWindow::revealMessage(const QString &id, const QString &folder, qint64 validity, qint64 uid)
{
    m_accountId = id; m_folder = folder; m_revealUid = uid; m_revealValidity = validity;
    rebuildFolders(); rebuildMessages();
    emit refreshRequested(id, folder);
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
