#include "mainwindow.h"

#include "messageviewer.h"
#include "aboutdialog.h"
#include "core/threading.h"
#include "core/displaytime.h"
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
#include "visiblesplitter.h"
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTreeView>
#include <QTimer>
#include <QItemSelectionModel>
#include <QStyle>
#include <QScreen>
#include <QMessageBox>
#include <QStyledItemDelegate>
#include <QPersistentModelIndex>
#include <QUuid>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include "core/diagnostics.h"
#include <algorithm>
#include <functional>

namespace smoothmail {
namespace {

class MessageTreeView final : public QTreeView
{
class PageNumberBox final : public QSpinBox
{
public:
    using QSpinBox::QSpinBox;
    QValidator::State validate(QString &text, int &) const override
    {
        if (text.isEmpty()) return QValidator::Intermediate;
        for (const auto ch : text) if (ch < u'0' || ch > u'9') return QValidator::Invalid;
        bool ok = false;
        const auto value = text.toULongLong(&ok);
        return ok && value >= quint64(minimum()) && value <= quint64(maximum())
            ? QValidator::Acceptable : QValidator::Intermediate;
    }
    void fixup(QString &text) const override
    {
        bool ok = false;
        const auto value = text.toULongLong(&ok);
        text = QString::number(text.isEmpty() ? minimum() : !ok ? maximum()
            : int(qBound<quint64>(minimum(), value, maximum())));
    }
};

public:
    using QTreeView::QTreeView;
    void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible) override
    {
        // Selection should reveal the row vertically, not align a wide subject
        // cell with the viewport. Explicit horizontal scrolling stays available.
        const int horizontal = horizontalScrollBar()->value();
        QTreeView::scrollTo(index, hint);
        horizontalScrollBar()->setValue(horizontal);
    }
};

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

QRect badgeRect(const QRect &cell)
{
    const int size = qMax(0, qMin(16, cell.height() - 4));
    return QRect(cell.center().x() - size / 2, cell.center().y() - size / 2, size, size);
}

class BadgeDelegate final : public QStyledItemDelegate
{
public:
    BadgeDelegate(QObject *parent, std::function<void(const QModelIndex &)> toggle)
        : QStyledItemDelegate(parent), m_toggle(std::move(toggle)) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem background(option); initStyleOption(&background, index); background.text.clear();
        const auto *widget = option.widget;
        (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);
        painter->save();
        auto font = option.font; font.setPixelSize(14); painter->setFont(font);
        painter->setPen(option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
        painter->drawText(badgeRect(option.rect), Qt::AlignCenter, index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
    bool editorEvent(QEvent *event, QAbstractItemModel *, const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (index.column() > 1 || event->type() != QEvent::MouseButtonRelease) return false;
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || !badgeRect(option.rect).contains(mouse->position().toPoint())) return false;
        m_toggle(index); return true;
    }
private:
    std::function<void(const QModelIndex &)> m_toggle;
};

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
        m_lastUpdated->setText(QStringLiteral("最終更新: %1 (%2)").arg(displayUpdateTime(m_lastSuccess), ago));
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
    findChild<QAction *>(QStringLiteral("undoAction"))->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    findChild<QAction *>(QStringLiteral("redoAction"))->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    auto *filterSearch = edit->addAction(QStringLiteral("フィルタ検索")); filterSearch->setObjectName(QStringLiteral("filterSearchAction")); filterSearch->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F));
    connect(filterSearch, &QAction::triggered, this, [this] { m_filter->setFocus(); m_filter->selectAll(); });
    auto *bodySearch = edit->addAction(QStringLiteral("本文内を検索")); bodySearch->setObjectName(QStringLiteral("bodySearchAction")); bodySearch->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    connect(bodySearch, &QAction::triggered, this, [this] { findChild<MessageViewer *>()->showSearch(); });

    auto *view = menuBar()->addMenu(QStringLiteral("表示"));
    auto *threads = pendingAction(view, QStringLiteral("スレッド表示"), "threadAction");
    threads->setCheckable(true);
    threads->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(threads, &QAction::toggled, this, [this] { rebuildMessages(); });

    auto *mail = menuBar()->addMenu(QStringLiteral("メール"));
    auto *refresh = pendingAction(mail, QStringLiteral("最新の情報を取得"), "refreshAction");
    refresh->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refresh, &QAction::triggered, this, &MainWindow::refreshAllRequested);
    auto *fullRefresh = pendingAction(mail, QStringLiteral("最新の全ての情報を取得"), "fullRefreshAction");
    fullRefresh->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F5));
    connect(fullRefresh, &QAction::triggered, this, &MainWindow::fullRefreshRequested);
    auto *automatic = pendingAction(mail, QStringLiteral("自動で更新"), "autoRefreshAction");
    automatic->setCheckable(true);
    auto *poll = new QTimer(this);
    m_poll = poll;
    poll->setInterval(30000);
    connect(automatic, &QAction::toggled, this, [poll](bool enabled) { if (enabled) poll->start(); else poll->stop(); });
    connect(poll, &QTimer::timeout, this, &MainWindow::refreshAllRequested);
    auto *autoRead = pendingAction(mail, QStringLiteral("自動で既読を付ける"), "autoReadAction");
    autoRead->setCheckable(true); // Opt in; do not silently mark mail read by default.
    mail->addSeparator();
    auto *compose = pendingAction(mail, QStringLiteral("新規メール"), "composeAction");
    compose->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    auto *reply = pendingAction(mail, QStringLiteral("新規返信メール"), "replyAction");
    reply->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(compose, &QAction::triggered,
             this, [this] { emit composeRequested(false); });
    connect(reply, &QAction::triggered,
            this, [this] { emit composeRequested(true); });

    auto *tools = menuBar()->addMenu(QStringLiteral("ツール"));
    connect(pendingAction(tools, QStringLiteral("ダウンロード履歴"), "downloadHistoryAction"), &QAction::triggered,
        this, &MainWindow::historyRequested);
    findChild<QAction *>(QStringLiteral("downloadHistoryAction"))->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    auto *settings = menuBar()->addMenu(QStringLiteral("設定"));
    auto *addAccount = pendingAction(settings, QStringLiteral("アカウントの追加"), "addAccountAction");
    connect(addAccount, &QAction::triggered, this, &MainWindow::addAccountRequested);
    auto *system = settings->addAction(QStringLiteral("システム設定")); system->setObjectName(QStringLiteral("systemSettingsAction")); connect(system, &QAction::triggered, this, &MainWindow::systemSettingsRequested);
    auto *help = menuBar()->addMenu(QStringLiteral("ヘルプ"));
    auto *about = help->addAction(QStringLiteral("このソフトウェアについて..."));
    about->setObjectName(QStringLiteral("aboutAction"));
    connect(about, &QAction::triggered, this, [this] {
        if (auto *dialog = findChild<AboutDialog *>()) {
            dialog->show(); dialog->raise(); dialog->activateWindow(); return;
        }
        auto *dialog = new AboutDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    });
}

void MainWindow::createPanes()
{
    auto *horizontal = new VisibleSplitter(Qt::Horizontal, this);
    horizontal->setObjectName(QStringLiteral("folderMessageSplitter"));
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
    connect(folderModel, &FolderModel::folderMoveRequested, this, &MainWindow::folderMoveRequested);
    connect(folderModel, &FolderModel::messageMoveRequested, this, &MainWindow::messageMoveRequested);
    connect(folderModel, &FolderModel::messageMoveBatchStarted, this, &MainWindow::messageMoveBatchStarted);
    connect(folderModel, &FolderModel::messageMoveBatchFinished, this, &MainWindow::messageMoveBatchFinished);
    folders->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(folders, &QTreeView::customContextMenuRequested, this, [this, folders](const QPoint &position) {
        const auto index = folders->indexAt(position);
        if (!index.isValid()) return;
        const auto id = index.data(Qt::UserRole).toString();
        const auto folder = index.data(Qt::UserRole + 1).toString();
        Account account;
        for (const auto &candidate : m_accounts) if (candidate.id == id) account = candidate;
        QMenu menu(this);
        auto *create = menu.addAction(QStringLiteral("新規フォルダ"));
        menu.addSeparator();
        auto *recent = menu.addAction(QStringLiteral("最新の情報を取得"));
        auto *full = menu.addAction(QStringLiteral("最新の全ての情報を取得"));
        recent->setObjectName(QStringLiteral("targetRecentRefresh")); full->setObjectName(QStringLiteral("targetFullRefresh"));
        const bool realFolder = folder.isEmpty() || bool(index.flags() & Qt::ItemIsSelectable);
        recent->setEnabled(realFolder); full->setEnabled(realFolder);
        menu.addSeparator();
        auto *edit = menu.addAction(folder.isEmpty() ? QStringLiteral("アカウントの編集") : QStringLiteral("名前の変更"));
        auto *remove = menu.addAction(folder.isEmpty() ? QStringLiteral("アカウントの削除") : QStringLiteral("削除"));
        menu.addSeparator();
        auto *smtp = menu.addAction(QStringLiteral("SMTPのOAuthを再認可"));
        auto *imap = menu.addAction(QStringLiteral("IMAP4のOAuthを再認可"));
        smtp->setEnabled(account.smtp.authentication == AuthenticationMode::OAuth2);
        imap->setEnabled(account.imap.authentication == AuthenticationMode::OAuth2);
        smtp->setVisible(folder.isEmpty()); imap->setVisible(folder.isEmpty());
        const auto *chosen = menu.exec(folders->viewport()->mapToGlobal(position));
        if (chosen == create) editFolder(index, true);
        if (chosen == recent || chosen == full) emit targetRefreshRequested(id, folder, chosen == full);
        if (chosen == edit) { if (folder.isEmpty()) emit editAccountRequested(id); else editFolder(index, false); }
        if (chosen == remove) {
            QMessageBox warning(QMessageBox::Warning, QStringLiteral("削除の確認"),
                folder.isEmpty() ? QStringLiteral("このアカウントの設定・保存済みメール・秘密情報をこのPCから削除します。サーバ上のメールとダウンロード済みファイルは削除しません。続けますか？")
                    : QStringLiteral("このフォルダをローカルで非表示にし、次の同期でサーバから削除します。メールが失われる可能性があります。子フォルダがある場合はサーバの規則により失敗することがあります。続けますか？"),
                QMessageBox::Yes | QMessageBox::Cancel, this);
            warning.setTextFormat(Qt::PlainText); warning.setDefaultButton(QMessageBox::Cancel);
            if (warning.exec() == QMessageBox::Yes) {
                if (folder.isEmpty()) emit deleteAccountRequested(id);
                else emit folderOperationRequested(id, QStringLiteral("delete"), folder, {});
            }
        }
        if (chosen == smtp) emit reauthorizeRequested(id, QStringLiteral("smtp"));
        if (chosen == imap) emit reauthorizeRequested(id, QStringLiteral("imap"));
    });
    connect(folderModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem *item) {
        if (m_rebuilding || !item->data(Qt::UserRole + 5).toBool()) return;
        const auto id = item->data(Qt::UserRole).toString();
        const auto source = item->data(Qt::UserRole + 1).toString();
        const auto prefix = item->data(Qt::UserRole + 3).toString();
        const auto separator = item->data(Qt::UserRole + 4).toString();
        const auto name = item->text().trimmed();
        item->setData(false, Qt::UserRole + 5);
        if (name.isEmpty() || name.contains(u'\r') || name.contains(u'\n') || name.contains(QChar::Null)
            || (!separator.isEmpty() && name.contains(separator))) {
            statusBar()->showMessage(QStringLiteral("フォルダ名が不正です。"));
        } else if (source != prefix + name) {
            emit folderOperationRequested(id, QStringLiteral("rename"), source, prefix + name);
        }
        QTimer::singleShot(0, this, [this] { rebuildFolders(); });
    });
    connect(folders->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this](QWidget *, QAbstractItemDelegate::EndEditHint) {
        QTimer::singleShot(0, this, [this] { rebuildFolders(); });
    });
    connect(folders->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &index) {
        if (m_rebuilding || !index.isValid()) return;
        // A context-menu click must not start a refresh that immediately makes
        // its own edit/delete operation unavailable.
        if (QApplication::mouseButtons().testFlag(Qt::RightButton)) return;
        const auto id = index.data(Qt::UserRole).toString();
        const auto name = index.data(Qt::UserRole + 1).toString();
        if (id.isEmpty()) return;
        m_accountId = id;
        m_folder = name;
        m_revealUid = 0; m_revealValidity = 0;
        if (m_pagedStorage) { m_loadedMessage.reset(); findChild<MessageViewer *>()->showLoading(); requestPage(true); }
        findChild<QAction *>(QStringLiteral("composeAction"))->setEnabled(!name.isEmpty());
        if (!m_pagedStorage) rebuildMessages();
        if (!name.isEmpty()) emit refreshRequested(id, name);
    });

    auto *vertical = new VisibleSplitter(Qt::Vertical, horizontal);
    vertical->setObjectName(QStringLiteral("messageViewerSplitter"));
    vertical->setObjectName(QStringLiteral("mailSplitter"));
    auto *listPane = new QWidget(vertical);
    auto *listLayout = new QVBoxLayout(listPane); listLayout->setContentsMargins(0, 0, 0, 0);
    auto *toolbar = new QHBoxLayout;
    m_filter = new QLineEdit(listPane); m_filter->setObjectName(QStringLiteral("mailFilter")); m_filter->setPlaceholderText(QStringLiteral("フィルタ文字列...")); m_filter->setMaximumWidth(180);
    auto *search = new QPushButton(QStringLiteral("検索"), listPane); search->setObjectName(QStringLiteral("mailSearch"));
    m_importantOnly = new QCheckBox(QStringLiteral("重要のみ"), listPane); m_importantOnly->setObjectName(QStringLiteral("importantOnly"));
    m_unreadOnly = new QCheckBox(QStringLiteral("未読のみ"), listPane); m_unreadOnly->setObjectName(QStringLiteral("unreadOnly"));
    m_filterProgress = new QProgressBar(listPane); m_filterProgress->setObjectName(QStringLiteral("filterProgress")); m_filterProgress->setRange(0, 0); m_filterProgress->setMaximumWidth(60); m_filterProgress->hide();
    m_pageRange = new QLabel(QStringLiteral("0 - 0 / 0件"), listPane); m_pageRange->setObjectName(QStringLiteral("pageRange"));
    m_page = new PageNumberBox(listPane); m_page->setObjectName(QStringLiteral("mailPage")); m_page->setRange(1, 1); m_page->setEnabled(false); m_page->setKeyboardTracking(false);
    m_page->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_page->setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
    m_previousPage = new QPushButton(listPane); m_previousPage->setObjectName(QStringLiteral("previousPage"));
    m_nextPage = new QPushButton(listPane); m_nextPage->setObjectName(QStringLiteral("nextPage"));
    m_previousPage->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    m_nextPage->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    m_previousPage->setToolTip(QStringLiteral("前のページ")); m_nextPage->setToolTip(QStringLiteral("次のページ"));
    m_previousPage->setAccessibleName(m_previousPage->toolTip()); m_nextPage->setAccessibleName(m_nextPage->toolTip());
    m_previousPage->setEnabled(false); m_nextPage->setEnabled(false);
    m_pageCount = new QLabel(QStringLiteral("/ 1"), listPane); m_pageCount->setObjectName(QStringLiteral("pageCount"));
    toolbar->addWidget(m_filter, 1); toolbar->addWidget(search); toolbar->addWidget(m_importantOnly); toolbar->addWidget(m_unreadOnly); toolbar->addWidget(m_filterProgress);
    toolbar->addStretch(); toolbar->addWidget(m_pageRange); toolbar->addWidget(m_previousPage); toolbar->addWidget(m_page); toolbar->addWidget(m_pageCount); toolbar->addWidget(m_nextPage);
    listLayout->addLayout(toolbar);
    const auto applyFilter = [this] { m_filterText = m_filter->text(); requestPage(true); };
    connect(search, &QPushButton::clicked, this, applyFilter); connect(m_filter, &QLineEdit::returnPressed, this, applyFilter);
    connect(m_importantOnly, &QCheckBox::toggled, this, [this] { requestPage(true); });
    connect(m_unreadOnly, &QCheckBox::toggled, this, [this] { requestPage(true); });
    connect(m_previousPage, &QPushButton::clicked, m_page, &QSpinBox::stepDown);
    connect(m_nextPage, &QPushButton::clicked, m_page, &QSpinBox::stepUp);
    connect(m_page, &QSpinBox::valueChanged, this, [this] { updatePageButtons(); requestPage(); });
    auto *messages = new MessageTreeView(listPane); listLayout->addWidget(messages);
    m_messageView = messages;
    messages->setObjectName(QStringLiteral("messageList"));
    messages->installEventFilter(this);
    auto *messageModel = new MessageListModel(messages);
    m_messageModel = messageModel;
    messageModel->setHorizontalHeaderLabels({
        QStringLiteral("重要"), QStringLiteral("未読"), QStringLiteral("添付"),
        QStringLiteral("メールタイトル"), QStringLiteral("差出人名"),
        QStringLiteral("差出人アドレス"), QStringLiteral("日時")});
    messages->setModel(messageModel);
    messages->setDragEnabled(true); messages->setDragDropMode(QAbstractItemView::DragOnly); messages->setDefaultDropAction(Qt::CopyAction); messages->setSelectionMode(QAbstractItemView::ExtendedSelection);
    messages->setUniformRowHeights(true);
    messages->setTreePosition(3);
    auto *badges = new BadgeDelegate(messages, [this](const QModelIndex &index) {
        const int row = index.data(Qt::UserRole).toInt();
        if (row < 0 || row >= m_visibleMessages.size()) return;
        const auto &identity = m_visibleMessages.at(row);
        changeFlag(row, index.column() == 0, index.column() == 0 ? !identity.important : !identity.seen);
    });
    for (int c = 0; c < 3; ++c) {
        messages->setItemDelegateForColumn(c, badges);
        messages->header()->setSectionResizeMode(c, QHeaderView::Fixed);
        const int width = messages->fontMetrics().horizontalAdvance(messageModel->headerData(c, Qt::Horizontal).toString()) + 12;
        messages->setColumnWidth(c, qMax(width, 24));
    }
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
            if (m_pagedStorage) requestPage(true); else messageModel->sort(m_sortColumn, m_sortOrder);
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
        if (m_rebuilding) return;
        const int record = index.siblingAtColumn(0).data(Qt::UserRole).toInt();
        findChild<QAction *>(QStringLiteral("replyAction"))->setEnabled(index.isValid());
        if (!index.isValid() || record < 0 || record >= m_visibleMessages.size()) { viewer->clear(); return; }
        if (m_pagedStorage) {
            const auto &identity = m_visibleMessages.at(record);
            if (m_loadedMessage && m_loadedMessage->accountId == identity.accountId && m_loadedMessage->folder == identity.folder
                && m_loadedMessage->uidValidity == identity.uidValidity && m_loadedMessage->uid == identity.uid && m_loadedMessage->storageFolder == identity.storageFolder) viewer->displayMessage(m_loadedMessage->document);
            else {
                m_loadedMessage.reset(); viewer->showLoading(); findChild<QAction *>(QStringLiteral("replyAction"))->setEnabled(false);
                emit messageRequested(identity);
            }
        } else viewer->displayMessage(m_visibleMessages.at(record).document);
        if (findChild<QAction *>(QStringLiteral("autoReadAction"))->isChecked() && !m_visibleMessages.at(record).seen
            && !(QApplication::mouseButtons().testFlag(Qt::LeftButton) && index.column() < 2)) changeFlag(record, false, true, true);
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
    for (const auto &old : m_accounts) {
        bool present = false;
        for (const auto &account : accounts) if (old.id == account.id) present = true;
        if (!present) clearAccountCache(old.id);
    }
    m_accounts = accounts;
    rebuildFolders();
    for (const auto *name : {"refreshAction", "fullRefreshAction", "autoRefreshAction", "autoReadAction", "threadAction"}) {
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
    if (!error.isEmpty()) {
        if (m_pendingCreateId == id) cancelPendingFolderEdit();
        m_syncErrors[id] = error; rebuildFolders();
        diagnostics::event("snapshot-error");
        statusBar()->showMessage(QStringLiteral("更新に失敗しました: ") + error); return;
    }
    if (remoteUpdate) m_syncErrors.remove(id);
    diagnostics::event("snapshot-apply", messages.size());
    m_folderCache[id] = folders;
    QStringList present;
    for (const auto &item : folders) present.append(item.name);
    for (const auto &cached : m_mailCache[id].keys()) if (!present.contains(cached)) m_mailCache[id].remove(cached);
    if (!m_pagedStorage) m_mailCache[id][folder] = messages;
    const bool visible = m_accountId.isEmpty() || (m_accountId == id && (m_folder.isEmpty() || m_folder == folder || !present.contains(m_folder)));
    if (visible) { m_accountId = id; m_folder = folder; }
    findChild<QAction *>(QStringLiteral("composeAction"))->setEnabled(!m_accountId.isEmpty() && !m_folder.isEmpty());
    if (remoteUpdate) m_lastSuccess = QDateTime::currentDateTime();
    rebuildFolders();
    if (visible) { if (m_pagedStorage) requestPage(); else rebuildMessages(); }
    if (m_pendingCreateId == id && present.contains(m_pendingCreateName)) {
        const auto name = m_pendingCreateName;
        cancelPendingFolderEdit();
        QTimer::singleShot(0, this, [this, id, name] {
            const auto matches = m_folders->match(m_folders->index(0, 0), Qt::UserRole + 1, name, -1, Qt::MatchExactly | Qt::MatchRecursive);
            for (const auto &index : matches) if (index.data(Qt::UserRole).toString() == id) { editFolder(index, false); break; }
        });
    }
    if (!m_pagedStorage && visible && remoteUpdate && m_revealUid > 0) {
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
    const auto &identity = m_visibleMessages.at(record);
    if (m_pagedStorage && m_loadedMessage && m_loadedMessage->accountId == identity.accountId && m_loadedMessage->folder == identity.folder
        && m_loadedMessage->uidValidity == identity.uidValidity && m_loadedMessage->uid == identity.uid && m_loadedMessage->storageFolder == identity.storageFolder) return m_loadedMessage;
    return identity;
}

void MainWindow::requestPage(bool reset)
{
    if (!m_pagedStorage) return;
    if (m_accountId.isEmpty() || m_folder.isEmpty()) {
        ++m_queryGeneration; m_filterProgress->hide(); m_loadedMessage.reset(); m_mailCache.clear();
        { const QSignalBlocker block(m_page); m_page->setRange(1, 1); m_page->setValue(1); m_page->setEnabled(false); }
        m_pageRange->setText(QStringLiteral("0 - 0 / 0件")); m_pageCount->setText(QStringLiteral("/ 1")); rebuildMessages(); return;
    }
    if (reset) { const QSignalBlocker block(m_page); m_page->setValue(1); }
    MailQuery query; query.accountId = m_accountId; query.folder = m_folder; query.text = m_filterText;
    query.page = m_page->value(); query.sortColumn = m_sortColumn; query.order = m_sortOrder;
    query.importantOnly = m_importantOnly->isChecked(); query.unreadOnly = m_unreadOnly->isChecked(); query.generation = ++m_queryGeneration;
    query.revealUid = m_revealUid; query.revealValidity = m_revealValidity;
    query.revealStorageFolder = m_revealStorageFolder;
    query.pageSize = m_pageSize;
    m_filterProgress->show(); emit pageRequested(query);
}
void MainWindow::applyPage(const MailPage &page)
{
    if (page.query.generation != m_queryGeneration || page.query.accountId != m_accountId || page.query.folder != m_folder) return;
    m_filterProgress->hide();
    if (!page.error.isEmpty()) { if (!m_loadedMessage) findChild<MessageViewer *>()->showFailure(); statusBar()->showMessage(QStringLiteral("検索に失敗しました: ") + page.error); return; }
    const auto pages = qMax<qint64>(1, (page.total + page.query.pageSize - 1) / page.query.pageSize);
    { const QSignalBlocker block(m_page); m_page->setRange(1, int(pages)); m_page->setValue(page.query.page); m_page->setEnabled(pages > 1); }
    m_pageCount->setText(QStringLiteral("/ %1").arg(pages));
    const auto start = page.total ? qint64(page.query.page - 1) * page.query.pageSize + 1 : 0;
    m_pageRange->setText(QStringLiteral("%1 - %2 / %3件").arg(start).arg(page.total ? start + page.messages.size() - 1 : 0).arg(page.total));
    m_mailCache.clear(); m_mailCache[m_accountId][m_folder] = page.messages;
    if (m_loadedMessage) {
        bool present = false;
        for (const auto &message : page.messages) if (message.accountId == m_loadedMessage->accountId && message.folder == m_loadedMessage->folder
            && message.uidValidity == m_loadedMessage->uidValidity && message.uid == m_loadedMessage->uid && message.storageFolder == m_loadedMessage->storageFolder) {
            m_loadedMessage->seen = message.seen; m_loadedMessage->important = message.important; present = true; break;
        }
        if (!present) m_loadedMessage.reset();
    }
    rebuildMessages();
        updatePageButtons();
    if (m_edgeSelection && m_messageModel->rowCount()) {
        auto index = m_messageModel->index(m_edgeSelection > 0 ? 0 : m_messageModel->rowCount() - 1, 0);
        if (m_edgeSelection < 0) while (m_messageView->isExpanded(index) && m_messageModel->rowCount(index)) index = m_messageModel->index(m_messageModel->rowCount(index) - 1, 0, index);
    updatePageButtons();
        m_messageView->setCurrentIndex(index.siblingAtColumn(3)); m_messageView->scrollTo(index);
    }
    m_edgeSelection = 0;
    if (page.query.revealUid > 0 && m_revealUid > 0) {
        m_revealUid = 0; statusBar()->showMessage(QStringLiteral("元メッセージが見つからないか、UIDVALIDITYが変更されています。"));
    }
}
void MainWindow::displayLoadedMessage(const MailRecord &message, const QString &error)
{
    const auto selected = selectedMessage();
    if (!selected || selected->accountId != message.accountId || selected->folder != message.folder
        || selected->uidValidity != message.uidValidity || selected->uid != message.uid || selected->storageFolder != message.storageFolder) return;
    if (!error.isEmpty()) { m_loadedMessage.reset(); findChild<MessageViewer *>()->showFailure(); findChild<QAction *>(QStringLiteral("replyAction"))->setEnabled(false); statusBar()->showMessage(error); return; }
    m_loadedMessage = message;
    // A body request may have started before a local flag edit.
    updatePageButtons();
    m_loadedMessage->seen = selected->seen; m_loadedMessage->important = selected->important;
    findChild<MessageViewer *>()->displayMessage(message.document);
    findChild<QAction *>(QStringLiteral("replyAction"))->setEnabled(true);
}
void MainWindow::showSyncProgress(const QString &id, qint64 done, qint64 total)
{
    for (const auto &account : m_accounts) if (account.id == id)
        statusBar()->showMessage(QStringLiteral("%1 のデータを取得中... %2 / %3件").arg(account.email).arg(done).arg(total));
}
void MainWindow::applySettings(const SystemSettings &settings)
{
    if (!validSettings(settings)) return;
    const bool changed = m_pageSize != settings.pageSize; m_pageSize = settings.pageSize; m_poll->setInterval(settings.refreshSeconds * 1000);
    if (changed) requestPage(true);
}
QChar MainWindow::folderSeparator(const QString &id, const QString &name) const
{
    const auto folders = m_folderCache.value(id); for (const auto &folder : folders) if (folder.name == name) return folder.separator;
    return folders.isEmpty() ? QChar(u'/') : folders.first().separator;
}
bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_messageView && event->type() == QEvent::KeyPress && m_pagedStorage) {
void MainWindow::updatePageButtons()
{
    m_previousPage->setEnabled(m_page->value() > m_page->minimum());
    m_nextPage->setEnabled(m_page->value() < m_page->maximum());
}
        const auto *key = static_cast<QKeyEvent *>(event); const auto current = m_messageView->currentIndex();
        if (current.isValid() && key->modifiers() == Qt::NoModifier) {
            if (key->key() == Qt::Key_Up && !m_messageView->indexAbove(current).isValid() && m_page->value() > 1) { m_edgeSelection = -1; m_page->setValue(m_page->value() - 1); return true; }
            if (key->key() == Qt::Key_Down && !m_messageView->indexBelow(current).isValid() && m_page->value() < m_page->maximum()) { m_edgeSelection = 1; m_page->setValue(m_page->value() + 1); return true; }
        }
    }
    return QMainWindow::eventFilter(object, event);
}

void MainWindow::rebuildFolders()
{
    m_rebuilding = true;
    m_folders->removeRows(0, m_folders->rowCount());
    QModelIndex selected;
    for (const auto &account : m_accounts) {
        auto *root = new QStandardItem(account.email);
        root->setEditable(false);
        root->setDragEnabled(true); root->setDropEnabled(true);
        root->setData(account.id, Qt::UserRole);
        if (m_refreshing.contains(account.id)) root->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        else if (m_syncErrors.contains(account.id)) root->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
        if (m_syncErrors.contains(account.id)) root->setToolTip(QStringLiteral("<qt>")
            + (QStringLiteral("直近の更新に失敗しました: ") + m_syncErrors.value(account.id)).toHtmlEscaped() + QStringLiteral("</qt>"));
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
                    node->setDragEnabled(true); node->setDropEnabled(true);
                    node->setData(folder.separator.isNull() ? QString{} : QString(folder.separator), Qt::UserRole + 4);
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

void MainWindow::clearAccountCache(const QString &id)
{
    if (m_accountId == id) { ++m_queryGeneration; m_loadedMessage.reset(); }
    m_mailCache.remove(id); m_folderCache.remove(id); m_syncErrors.remove(id); m_refreshing.remove(id);
    if (m_accountId == id) {
        m_accountId.clear(); m_folder.clear(); rebuildMessages();
        if (m_pagedStorage) requestPage();
        findChild<QAction *>(QStringLiteral("composeAction"))->setEnabled(false);
    }
}
void MainWindow::followFolderRename(const QString &id, const QString &source, const QString &destination)
{
    if (m_accountId != id) return;
    QChar separator; for (const auto &folder : m_folderCache.value(id)) if (folder.name == source) separator = folder.separator;
    if (m_folder != source && (separator.isNull() || !m_folder.startsWith(source + separator))) return;
    m_folder = destination + m_folder.mid(source.size()); m_loadedMessage.reset(); requestPage();
}

void MainWindow::editFolder(const QModelIndex &index, bool create)
{
    const auto id = index.data(Qt::UserRole).toString();
    const auto source = index.data(Qt::UserRole + 1).toString();
    QChar separator;
    for (const auto &folder : m_folderCache.value(id)) {
        if (folder.name == source || source.isEmpty()) { separator = folder.separator; break; }
    }
    if (create && !source.isEmpty() && separator.isNull()) {
        statusBar()->showMessage(QStringLiteral("このサーバのフォルダには子フォルダを作成できません。")); return;
    }
    if (create) {
        // IMAP does not allow an unnamed mailbox. Create an empty, uniquely
        // named mailbox first, then open inline rename after LIST confirms it.
        const auto prefix = source.isEmpty() ? QString{} : source + separator;
        m_pendingCreateId = id;
        m_pendingCreateName = prefix + QStringLiteral("新しいフォルダ-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        emit folderOperationRequested(id, QStringLiteral("create"), {}, m_pendingCreateName);
        return;
    }
    m_rebuilding = true;
    QStandardItem *item = m_folders->itemFromIndex(index);
    QString prefix;
    if (!separator.isNull() && source.contains(separator)) prefix = source.left(source.lastIndexOf(separator) + 1);
    item->setText(source.mid(prefix.size()));
    item->setData(prefix, Qt::UserRole + 3);
    item->setData(separator.isNull() ? QString{} : QString(separator), Qt::UserRole + 4);
    item->setEditable(true);
    item->setData(true, Qt::UserRole + 5);
    m_rebuilding = false;
    m_folderView->expand(index); m_folderView->edit(item->index());
}

void MainWindow::rebuildMessages()
{
    if (!m_messageModel) return;
    qint64 previousUid = 0, previousValidity = 0;
    QString previousAccount, previousFolder, previousStorage;
    const auto previous = m_messageView->currentIndex();
    const auto previousRow = previous.siblingAtColumn(0).data(Qt::UserRole).toInt();
    if (previous.isValid() && previousRow >= 0 && previousRow < m_visibleMessages.size()) {
        previousUid = m_visibleMessages.at(previousRow).uid;
        previousValidity = m_visibleMessages.at(previousRow).uidValidity;
        previousAccount = m_visibleMessages.at(previousRow).accountId;
        previousFolder = m_visibleMessages.at(previousRow).folder;
        previousStorage = m_visibleMessages.at(previousRow).storageFolder;
    }
    m_rebuilding = true;
    m_messageModel->removeRows(0, m_messageModel->rowCount());
    findChild<MessageViewer *>()->clear();
    m_visibleMessages = m_mailCache.value(m_accountId).value(m_folder);
    static_cast<MessageListModel *>(m_messageModel)->records = m_visibleMessages;
    QList<ThreadMessage> threads;
    for (const auto &record : m_visibleMessages) {
        const auto &doc = record.document;
        threads.append({record.accountId, record.storageFolder + u'/' + record.folder + u'/' + QString::number(record.uidValidity) + u'/' + QString::number(record.uid), doc.messageId, doc.inReplyTo, doc.references});
    }
    const auto accountCache = m_mailCache.value(m_accountId);
    for (auto it = accountCache.cbegin(); it != accountCache.cend(); ++it) {
        if (it.key() == m_folder) continue;
        for (const auto &record : it.value()) {
            const auto &doc = record.document;
            threads.append({record.accountId, record.storageFolder + u'/' + record.folder + u'/' + QString::number(record.uidValidity) + u'/' + QString::number(record.uid), doc.messageId, doc.inReplyTo, doc.references});
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
        const QStringList cells{record.important ? QStringLiteral("★") : QStringLiteral("☆"), record.seen ? QStringLiteral("○") : QStringLiteral("●"),
            doc.attachments.isEmpty() ? QString{} : QStringLiteral("●"), doc.subject, doc.senderName, doc.senderAddress,
            displayTime(doc.date)};
        QList<QStandardItem *> row;
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QStandardItem(cells.at(c));
            item->setEditable(false);
            item->setData(i, Qt::UserRole);
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
    if (!m_pagedStorage) m_messageModel->sort(m_sortColumn, m_sortOrder);
    if (m_revealUid > 0) { previousUid = m_revealUid; previousValidity = m_revealValidity; previousAccount = m_accountId; previousFolder = m_folder; previousStorage = m_revealStorageFolder; }
    for (qsizetype i = 0; i < m_visibleMessages.size(); ++i) {
        if (m_visibleMessages.at(i).uid == previousUid && m_visibleMessages.at(i).uidValidity == previousValidity
            && m_visibleMessages.at(i).accountId == previousAccount && m_visibleMessages.at(i).folder == previousFolder
            && ((m_revealUid > 0 && m_revealStorageFolder.isEmpty()) || m_visibleMessages.at(i).storageFolder == previousStorage)) {
            m_messageView->setCurrentIndex(rows.at(i).at(3)->index());
            m_revealUid = 0;
            break;
        }
    }
    m_rebuilding = false;
    for (int c = 3; c < 6; ++c) m_messageView->resizeColumnToContents(c);
    const auto current = m_messageView->currentIndex();
    findChild<QAction *>(QStringLiteral("replyAction"))->setEnabled(current.isValid() && (!m_pagedStorage || m_loadedMessage.has_value()));
    if (current.isValid()) {
        const int row = current.data(Qt::UserRole).toInt();
        if (row >= 0 && row < m_visibleMessages.size()) {
            if (m_pagedStorage) {
                if (m_loadedMessage && m_loadedMessage->accountId == m_accountId && m_loadedMessage->folder == m_folder
                    && m_loadedMessage->uidValidity == m_visibleMessages.at(row).uidValidity && m_loadedMessage->uid == m_visibleMessages.at(row).uid && m_loadedMessage->storageFolder == m_visibleMessages.at(row).storageFolder)
                    findChild<MessageViewer *>()->displayMessage(m_loadedMessage->document);
                else { findChild<MessageViewer *>()->showLoading(); emit messageRequested(m_visibleMessages.at(row)); }
            } else findChild<MessageViewer *>()->displayMessage(m_visibleMessages.at(row).document);
        }
    }
}

void MainWindow::changeFlag(int row, bool importantFlag, bool enabled, bool automatic)
{
    if (row < 0 || row >= m_visibleMessages.size()) return;
    const auto identity = m_visibleMessages.at(row);
    if ((importantFlag ? identity.important : identity.seen) == enabled) return;
    if (importantFlag) m_visibleMessages[row].important = enabled; else m_visibleMessages[row].seen = enabled;
    auto &cached = m_mailCache[m_accountId][m_folder];
    for (auto &record : cached) if (record.uidValidity == identity.uidValidity && record.uid == identity.uid && record.storageFolder == identity.storageFolder) record = m_visibleMessages.at(row);
    if (m_loadedMessage && m_loadedMessage->accountId == identity.accountId && m_loadedMessage->folder == identity.folder
        && m_loadedMessage->uidValidity == identity.uidValidity && m_loadedMessage->uid == identity.uid && m_loadedMessage->storageFolder == identity.storageFolder) {
        m_loadedMessage->seen = m_visibleMessages.at(row).seen; m_loadedMessage->important = m_visibleMessages.at(row).important;
    }
    if (!importantFlag) {
        for (auto &folder : m_folderCache[m_accountId]) if (folder.name == identity.folder) folder.unread = qMax<qint64>(0, folder.unread + (enabled ? -1 : 1));
        rebuildFolders();
    }
    // Update cells in place, without changing selection or rebuilding threads.
    QList<QModelIndex> parents{QModelIndex{}}, matches;
    while (!parents.isEmpty()) {
        const auto parent = parents.takeLast();
        for (int i = 0; i < m_messageModel->rowCount(parent); ++i) {
            const auto index = m_messageModel->index(i, 3, parent);
            if (index.data(Qt::UserRole).toInt() == row) matches.append(index);
            parents.append(index.siblingAtColumn(0));
        }
    }
    for (const auto &index : matches) {
        const auto &record = m_visibleMessages.at(row);
        for (int c = 0; c < 7; ++c) {
            auto *item = m_messageModel->itemFromIndex(index.siblingAtColumn(c));
            if (!item) continue;
            if (c == 0) item->setText(record.important ? QStringLiteral("★") : QStringLiteral("☆"));
            if (c == 1) item->setText(record.seen ? QStringLiteral("○") : QStringLiteral("●"));
            auto font = item->font(); font.setBold(!record.seen); item->setFont(font);
        }
    }
    static_cast<MessageListModel *>(m_messageModel)->records = m_visibleMessages;
    if (automatic) emit automaticFlagRequested(identity, importantFlag, enabled); else emit messageFlagRequested(identity, importantFlag, enabled);
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

void MainWindow::revealMessage(const QString &id, const QString &folder, qint64 validity, qint64 uid, const QString &storageFolder)
{
    m_accountId = id; m_folder = folder; m_revealUid = uid; m_revealValidity = validity;
    m_revealStorageFolder = storageFolder;
    if (m_pagedStorage) {
        m_filter->clear(); m_filterText.clear();
        { const QSignalBlocker a(m_importantOnly), b(m_unreadOnly); m_importantOnly->setChecked(false); m_unreadOnly->setChecked(false); }
        rebuildFolders(); requestPage(true); return;
    }
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
