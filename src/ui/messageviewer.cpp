#include "messageviewer.h"

#include "safebodyview.h"

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QListWidget>
#include <QStyle>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QKeyEvent>
#include <QTextCursor>
#include <QShortcut>

namespace smoothmail {

MessageViewer::MessageViewer(QWidget *parent)
    : QWidget(parent)
    , m_summary(new QLabel(this))
    , m_attachments(new QListWidget(this))
    , m_headerToggle(new QToolButton(this))
    , m_headers(new QPlainTextEdit(this))
    , m_body(new SafeBodyView(this))
{
    auto *layout = new QVBoxLayout(this);
    m_summary->setObjectName(QStringLiteral("messageSummary"));
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summary);
    m_attachments->setObjectName(QStringLiteral("messageAttachments"));
    m_attachments->setViewMode(QListView::IconMode);
    m_attachments->setFlow(QListView::LeftToRight);
    m_attachments->setMaximumHeight(110);
    m_attachments->setWrapping(true);
    layout->addWidget(m_attachments);
    connect(m_attachments, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        emit attachmentRequested(item->data(Qt::UserRole).toString());
    });

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    auto *headerRow = new QHBoxLayout;

    m_headerToggle->setObjectName(QStringLiteral("headerToggle"));
    m_headerToggle->setText(QStringLiteral("メールヘッダ"));
    m_headerToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_headerToggle->setCheckable(true);
    m_headerToggle->setAutoRaise(true);
    auto font = m_headerToggle->font(); font.setPointSizeF(qMax(7.0, font.pointSizeF() - 1)); m_headerToggle->setFont(font);
    m_headerToggle->setStyleSheet(QStringLiteral("QToolButton { border: 1px solid transparent; } QToolButton:hover { border: 1px solid palette(mid); }"));
    headerRow->addWidget(m_headerToggle); headerRow->addWidget(separator, 1); layout->addLayout(headerRow);
    m_headers->setObjectName(QStringLiteral("messageHeaders"));
    m_headers->setReadOnly(true);
    m_headers->setMaximumHeight(180);
    layout->addWidget(m_headers);
    connect(m_headerToggle, &QToolButton::toggled, this, [this](bool expanded) {
        m_headers->setVisible(expanded);
        m_headerToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });

    auto *bodySeparator = new QFrame(this);
    bodySeparator->setFrameShape(QFrame::HLine);
    layout->addWidget(bodySeparator);
    m_body->setObjectName(QStringLiteral("messageBody"));
    layout->addWidget(m_body, 1);
    m_search = new QWidget(this); m_search->setObjectName(QStringLiteral("bodySearchBar")); auto *searchLayout = new QHBoxLayout(m_search); searchLayout->setContentsMargins(0, 0, 0, 0);
    m_searchCount = new QLabel(QStringLiteral("0 / 0 件"), m_search); m_searchCount->setObjectName(QStringLiteral("bodySearchCount")); searchLayout->addWidget(m_searchCount); searchLayout->addStretch();
    m_searchText = new QLineEdit(m_search); m_searchText->setObjectName(QStringLiteral("bodySearchText")); m_searchText->setMaximumWidth(240); searchLayout->addWidget(m_searchText);
    auto *previous = new QToolButton(m_search); previous->setObjectName(QStringLiteral("bodySearchPrevious")); previous->setArrowType(Qt::UpArrow); previous->setToolTip(QStringLiteral("前の結果（Shift+Enter）"));
    auto *next = new QToolButton(m_search); next->setObjectName(QStringLiteral("bodySearchNext")); next->setArrowType(Qt::DownArrow); next->setToolTip(QStringLiteral("次の結果（Enter）"));
    auto *close = new QToolButton(m_search); close->setObjectName(QStringLiteral("bodySearchClose")); close->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton)); close->setToolTip(QStringLiteral("閉じる（Esc）"));
    searchLayout->addWidget(previous); searchLayout->addWidget(next); searchLayout->addWidget(close); layout->addWidget(m_search); m_search->hide();
    connect(m_searchText, &QLineEdit::textChanged, this, &MessageViewer::countMatches);
    connect(previous, &QToolButton::clicked, this, [this] { findMatch(true); }); connect(next, &QToolButton::clicked, this, [this] { findMatch(false); });
    connect(close, &QToolButton::clicked, this, [this] { m_search->hide(); m_body->setFocus(); });
    m_searchText->installEventFilter(this);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this); escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { m_search->hide(); m_body->setFocus(); });
    connect(m_body, &SafeBodyView::externalLinkRequested,
            this, &MessageViewer::externalLinkRequested);
    clear();
}

void MessageViewer::displayMessage(const MessageDocument &message)
{
    m_attachments->clear();
    for (const auto &attachment : message.attachments) {
        auto *item = new QListWidgetItem(style()->standardIcon(QStyle::SP_FileIcon),
            attachment.fileName.isEmpty() ? QStringLiteral("添付ファイル") : attachment.fileName, m_attachments);
        item->setData(Qt::UserRole, attachment.partId);
        item->setToolTip(QStringLiteral("Downloadsへ保存"));
    }
    m_attachments->setVisible(!message.attachments.isEmpty());
    m_summary->setText(QStringLiteral("タイトル: %1\n差出人名: %2 差出人アドレス: %3\n宛先: %4")
                           .arg(message.subject, message.senderName, message.senderAddress,
                                message.recipients.join(QStringLiteral(", "))));
    m_headerToggle->setEnabled(true);
    m_headerToggle->setChecked(false);
    m_headerToggle->setArrowType(Qt::RightArrow);
    m_headers->setPlainText(message.headers);
    m_headers->hide();

    QStringList bodies;
    for (const auto &part : message.parts) {
        bodies.append(part.text);
    }
    m_body->displayText(bodies.join(QStringLiteral("\n\n")));
    countMatches();
}

void MessageViewer::clear()
{
    m_summary->clear();
    m_attachments->clear(); m_attachments->hide();
    m_headerToggle->setChecked(false);
    m_headerToggle->setArrowType(Qt::RightArrow);
    m_headerToggle->setEnabled(false);
    m_headers->clear();
    m_headers->hide();
    m_body->displayText(QStringLiteral("表示するメールはありません。"));
    countMatches();
}

void MessageViewer::showSearch() { m_search->show(); m_searchText->setFocus(); m_searchText->selectAll(); }
bool MessageViewer::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_searchText && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) { m_search->hide(); m_body->setFocus(); return true; }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { findMatch(key->modifiers().testFlag(Qt::ShiftModifier)); return true; }
    }
    return QWidget::eventFilter(object, event);
}
void MessageViewer::countMatches()
{
    m_matches = 0; m_current = 0; m_position = -1;
    auto cursor = m_body->textCursor(); cursor.clearSelection(); m_body->setTextCursor(cursor);
    const auto needle = m_searchText->text(), text = m_body->toPlainText();
    if (!needle.isEmpty()) for (qsizetype pos = text.indexOf(needle, 0, Qt::CaseInsensitive); pos >= 0; pos = text.indexOf(needle, pos + needle.size(), Qt::CaseInsensitive)) ++m_matches;
    m_searchCount->setText(QStringLiteral("0 / %1 件").arg(m_matches));
    if (m_matches) findMatch(false);
}
void MessageViewer::findMatch(bool previous)
{
    if (!m_matches) return;
    const auto needle = m_searchText->text(), text = m_body->toPlainText();
    m_current = previous ? (m_current <= 1 ? m_matches : m_current - 1) : (m_current >= m_matches ? 1 : m_current + 1);
    qsizetype position = -1;
    for (int i = 0; i < m_current; ++i) position = text.indexOf(needle, position < 0 ? 0 : position + needle.size(), Qt::CaseInsensitive);
    if (position < 0) return;
    m_position = int(position); QTextCursor cursor(m_body->document()); cursor.setPosition(m_position); cursor.setPosition(m_position + int(needle.size()), QTextCursor::KeepAnchor); m_body->setTextCursor(cursor); m_body->ensureCursorVisible();
    m_searchCount->setText(QStringLiteral("%1 / %2 件").arg(m_current).arg(m_matches));
}

} // namespace smoothmail
