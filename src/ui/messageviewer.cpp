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
#include <QSplitter>
#include "visiblesplitter.h"
#include <QScrollBar>
#include <QTimer>
#include "core/displaytime.h"
#include <algorithm>

namespace smoothmail {
namespace {
QString valueHtml(const QString &value, bool address = false)
{
    if (value.isEmpty()) return QStringLiteral("<span style=\"color:gray\">(空)</span>");
    bool nonAscii = false; for (const auto c : value) if (c.unicode() > 127) nonAscii = true;
    return QStringLiteral("<span style=\"color:%1;font-weight:bold\">%2</span>")
        .arg(address && nonAscii ? QStringLiteral("red") : QStringLiteral("black"), value.toHtmlEscaped());
}
QString summaryHtml(const MessageDocument &message)
{
    QStringList recipients;
    for (const auto &address : message.recipients) recipients.append(valueHtml(address, true));
    return QStringLiteral("<span style=\"color:black\">件名: %1<br>日時: %2<br>差出人: %3 ( %4 ) 宛先: %5</span>")
        .arg(valueHtml(message.subject), valueHtml(displayTime(message.date, true)), valueHtml(message.senderName),
             valueHtml(message.senderAddress, true), recipients.isEmpty() ? valueHtml({}) : recipients.join(QStringLiteral(", ")));
}
}

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
    auto *splitter = new VisibleSplitter(Qt::Vertical, this); splitter->setObjectName(QStringLiteral("headerBodySplitter"));
    splitter->setChildrenCollapsible(false);
    auto *information = new QWidget(this); auto *informationLayout = new QVBoxLayout(information); informationLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(information);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    informationLayout->addWidget(m_summary);
    m_attachments->setObjectName(QStringLiteral("messageAttachments"));
    m_attachments->setViewMode(QListView::IconMode);
    m_attachments->setFlow(QListView::LeftToRight);
    m_attachments->setMaximumHeight(110);
    m_attachments->setWrapping(true);
    informationLayout->addWidget(m_attachments);
    connect(m_attachments, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        emit attachmentRequested(item->data(Qt::UserRole).toString());
    });

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    auto *headerRow = new QHBoxLayout;

    m_headerToggle->setObjectName(QStringLiteral("headerToggle"));
    m_headerToggle->setText(QStringLiteral("▶ メールヘッダ"));
    m_headerToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_headerToggle->setCheckable(true);
    m_headerToggle->setAutoRaise(true);
    auto font = m_summary->font();
    if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * 2.0 / 3.0);
    else font.setPixelSize(qMax(1, qRound(font.pixelSize() * 2.0 / 3.0)));
    m_headerToggle->setFont(font);
    m_headerToggle->setStyleSheet(QStringLiteral("QToolButton { border: none; background: transparent; } QToolButton:hover { background: palette(midlight); }"));
    headerRow->addWidget(m_headerToggle); headerRow->addWidget(separator, 1); informationLayout->addLayout(headerRow);
    m_headers->setObjectName(QStringLiteral("messageHeaders"));
    m_headers->setReadOnly(true);
    m_headers->setMinimumHeight(0);
    splitter->addWidget(m_headers);
    connect(m_headerToggle, &QToolButton::toggled, this, [this, splitter](bool expanded) {
        if (!expanded && !splitter->sizes().isEmpty() && splitter->sizes().first() > 0) m_headerHeight = splitter->sizes().first();
        m_headers->setVisible(expanded);
        m_headerToggle->setText(expanded ? QStringLiteral("▼ メールヘッダ") : QStringLiteral("▶ メールヘッダ"));
        if (expanded) {
            const int height = qMin(m_headerHeight, splitter->height() / 2);
            splitter->setSizes({height, qMax(1, splitter->height() - height)});
        }
    });

    m_body->setObjectName(QStringLiteral("messageBody"));
    splitter->addWidget(m_body); splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    connect(splitter, &QSplitter::splitterMoved, this, [this] { centerCurrentMatch(); });
    connect(m_body->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { if (m_searchText) highlightMatches(); });
    m_search = new QWidget(this); m_search->setObjectName(QStringLiteral("bodySearchBar")); auto *searchLayout = new QHBoxLayout(m_search); searchLayout->setContentsMargins(0, 0, 0, 0);
    m_searchCount = new QLabel(QStringLiteral("0 / 0 件"), m_search); m_searchCount->setObjectName(QStringLiteral("bodySearchCount")); searchLayout->addWidget(m_searchCount); searchLayout->addStretch();
    m_searchText = new QLineEdit(m_search); m_searchText->setObjectName(QStringLiteral("bodySearchText")); m_searchText->setMaximumWidth(240); searchLayout->addWidget(m_searchText);
    auto *previous = new QToolButton(m_search); previous->setObjectName(QStringLiteral("bodySearchPrevious")); previous->setArrowType(Qt::UpArrow); previous->setToolTip(QStringLiteral("前の結果（Shift+Enter）"));
    auto *next = new QToolButton(m_search); next->setObjectName(QStringLiteral("bodySearchNext")); next->setArrowType(Qt::DownArrow); next->setToolTip(QStringLiteral("次の結果（Enter）"));
    auto *close = new QToolButton(m_search); close->setObjectName(QStringLiteral("bodySearchClose")); close->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton)); close->setToolTip(QStringLiteral("閉じる（Esc）"));
    searchLayout->addWidget(previous); searchLayout->addWidget(next); searchLayout->addWidget(close); layout->addWidget(m_search); m_search->hide();
    connect(m_searchText, &QLineEdit::textChanged, this, &MessageViewer::countMatches);
    connect(previous, &QToolButton::clicked, this, [this] { findMatch(true); }); connect(next, &QToolButton::clicked, this, [this] { findMatch(false); });
    connect(close, &QToolButton::clicked, this, &MessageViewer::hideSearch);
    m_searchText->installEventFilter(this);
    m_body->installEventFilter(this);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this); escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &MessageViewer::hideSearch);
    connect(m_body, &SafeBodyView::externalLinkRequested,
            this, &MessageViewer::externalLinkRequested);
    clear();
}

void MessageViewer::displayMessage(const MessageDocument &message)
{
    m_hasMessage = true;
    m_matchPositions.clear(); m_position = -1; m_body->setExtraSelections({});
    m_attachments->clear();
    for (const auto &attachment : message.attachments) {
        auto *item = new QListWidgetItem(style()->standardIcon(QStyle::SP_FileIcon),
            attachment.fileName.isEmpty() ? QStringLiteral("添付ファイル") : attachment.fileName, m_attachments);
        item->setData(Qt::UserRole, attachment.partId);
        item->setToolTip(QStringLiteral("Downloadsへ保存"));
    }
    m_attachments->setVisible(!message.attachments.isEmpty());
    m_summary->setText(summaryHtml(message));
    m_headerToggle->setEnabled(true);
    m_headerToggle->setChecked(false);
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
    m_hasMessage = false;
    m_summary->clear();
    m_attachments->clear(); m_attachments->hide();
    m_headerToggle->setChecked(false);
    m_headerToggle->setText(QStringLiteral("▶ メールヘッダ"));
    m_headerToggle->setEnabled(false);
    m_headers->clear();
    m_headers->hide();
    m_body->displayText(QStringLiteral("表示するメールはありません。"));
    countMatches();
}

void MessageViewer::showLoading() { clear(); m_body->displayText(QStringLiteral("読み込み中...")); countMatches(); }
void MessageViewer::showFailure() { clear(); m_body->displayText(QStringLiteral("読み込みに失敗しました")); countMatches(); }
void MessageViewer::hideSearch() { m_search->hide(); m_body->setExtraSelections({}); m_body->setFocus(); }
void MessageViewer::showSearch() { m_search->show(); countMatches(); m_searchText->setFocus(); m_searchText->selectAll(); }
bool MessageViewer::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_searchText && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) { hideSearch(); return true; }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { findMatch(key->modifiers().testFlag(Qt::ShiftModifier)); return true; }
    }
    if (object == m_body && event->type() == QEvent::Resize) QTimer::singleShot(0, this, &MessageViewer::centerCurrentMatch);
    return QWidget::eventFilter(object, event);
}
void MessageViewer::countMatches()
{
    m_matches = 0; m_current = 0; m_position = -1;
    m_matchPositions.clear(); m_body->setExtraSelections({});
    auto cursor = m_body->textCursor(); cursor.clearSelection(); m_body->setTextCursor(cursor);
    const auto needle = m_searchText->text(), text = m_body->toPlainText();
    if (m_hasMessage && !needle.isEmpty()) for (qsizetype pos = text.indexOf(needle, 0, Qt::CaseInsensitive); pos >= 0; pos = text.indexOf(needle, pos + needle.size(), Qt::CaseInsensitive)) m_matchPositions.append(int(pos));
    m_matches = int(m_matchPositions.size());
    m_searchCount->setText(QStringLiteral("0 / %1 件").arg(m_matches));
    if (m_matches) findMatch(false);
}
void MessageViewer::findMatch(bool previous)
{
    if (!m_matches) return;
    m_current = previous ? (m_current <= 1 ? m_matches : m_current - 1) : (m_current >= m_matches ? 1 : m_current + 1);
    m_position = m_matchPositions.at(m_current - 1);
    QTextCursor cursor(m_body->document()); cursor.setPosition(m_position); m_body->setTextCursor(cursor);
    highlightMatches(); centerCurrentMatch(); QTimer::singleShot(0, this, &MessageViewer::centerCurrentMatch);
    m_searchCount->setText(QStringLiteral("%1 / %2 件").arg(m_current).arg(m_matches));
}
void MessageViewer::highlightMatches()
{
    if (!m_hasMessage) { m_body->setExtraSelections({}); return; }
    QList<QTextEdit::ExtraSelection> selections;
    // Render every visible hit, without allocating a QTextCursor/format for
    // millions of off-screen hits in a large body.
    const int first = qMax(0, m_body->cursorForPosition(QPoint(0, 0)).position() - int(m_searchText->text().size()));
    const int last = m_body->cursorForPosition(m_body->viewport()->rect().bottomRight()).position();
    auto begin = std::lower_bound(m_matchPositions.cbegin(), m_matchPositions.cend(), first);
    if (!m_search->isHidden()) for (auto it = begin; it != m_matchPositions.cend() && *it <= last; ++it) {
        const auto i = it - m_matchPositions.cbegin();
        QTextEdit::ExtraSelection selection; selection.cursor = QTextCursor(m_body->document());
        selection.cursor.setPosition(*it); selection.cursor.setPosition(*it + int(m_searchText->text().size()), QTextCursor::KeepAnchor);
        selection.format.setBackground(i == m_current - 1 ? QColor(Qt::blue) : QColor(QStringLiteral("#ffc0cb")));
        if (i == m_current - 1) selection.format.setForeground(Qt::white);
        selections.append(selection);
    }
    m_body->setExtraSelections(selections);
}
void MessageViewer::centerCurrentMatch()
{
    if (!m_hasMessage || m_position < 0 || m_search->isHidden()) return;
    QTextCursor cursor(m_body->document()); cursor.setPosition(m_position);
    auto *scroll = m_body->verticalScrollBar();
    scroll->setValue(scroll->value() + m_body->cursorRect(cursor).center().y() - m_body->viewport()->height() / 2);
    highlightMatches();
}

} // namespace smoothmail
