#include "messageviewer.h"

#include "safebodyview.h"

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QListWidget>
#include <QStyle>

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
    layout->addWidget(separator);

    m_headerToggle->setObjectName(QStringLiteral("headerToggle"));
    m_headerToggle->setText(QStringLiteral("メールヘッダ"));
    m_headerToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_headerToggle->setCheckable(true);
    layout->addWidget(m_headerToggle);
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
}

} // namespace smoothmail
