#include "composedialog.h"
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace smoothmail {
ComposeDialog::ComposeDialog(ComposeData value, QWidget *parent)
    : QDialog(parent), m_original(std::move(value)), m_subject(new QLineEdit(this)), m_from(new QLineEdit(this)),
      m_to(new QLineEdit(this)), m_cc(new QLineEdit(this)), m_bcc(new QLineEdit(this)), m_body(new QPlainTextEdit(this)),
      m_status(new QLabel(this)), m_send(new QPushButton(QStringLiteral("送信"), this)),
      m_save(new QPushButton(QStringLiteral("保存"), this)), m_cancel(new QPushButton(QStringLiteral("閉じる"), this))
{
    qRegisterMetaType<ComposeData>();
    m_from->setObjectName(QStringLiteral("composeFrom"));
    m_to->setObjectName(QStringLiteral("composeTo"));
    m_send->setObjectName(QStringLiteral("composeSend"));
    m_save->setObjectName(QStringLiteral("composeSave"));
    setWindowTitle(QStringLiteral("新規メール")); resize(700, 600);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_subject->setObjectName(QStringLiteral("composeSubject")); m_subject->setText(m_original.subject);
    m_from->setText(m_original.from); m_from->setReadOnly(true);
    m_to->setText(m_original.to); m_cc->setText(m_original.cc); m_bcc->setText(m_original.bcc);
    form->addRow(QStringLiteral("件名"), m_subject); form->addRow(QStringLiteral("送信元"), m_from);
    form->addRow(QStringLiteral("宛先(To)"), m_to); form->addRow(QStringLiteral("CC"), m_cc); form->addRow(QStringLiteral("BCC"), m_bcc);
    auto *reply = new QLineEdit(m_original.replySubject, this); reply->setReadOnly(true);
    form->addRow(QStringLiteral("返信元メール"), reply); layout->addLayout(form);
    m_body->setObjectName(QStringLiteral("composeBody")); m_body->setPlainText(m_original.body); layout->addWidget(m_body, 1);
    m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_send); buttons->addWidget(m_save); buttons->addStretch(); buttons->addWidget(m_cancel); layout->addLayout(buttons);
    connect(m_send, &QPushButton::clicked, this, [this] { submit(false); });
    connect(m_save, &QPushButton::clicked, this, [this] { submit(true); });
    connect(m_cancel, &QPushButton::clicked, this, &ComposeDialog::reject);
}

ComposeData ComposeDialog::draft() const
{
    auto result = m_original;
    result.subject = m_subject->text(); result.to = m_to->text(); result.cc = m_cc->text(); result.bcc = m_bcc->text();
    result.body = m_body->toPlainText();
    return result;
}

void ComposeDialog::submit(bool saveAsDraft)
{
    if (m_busy) return;
    m_busy = true;
    m_send->setEnabled(false); m_save->setEnabled(false); m_cancel->setEnabled(false);
    for (auto *edit : findChildren<QLineEdit *>()) edit->setEnabled(false);
    m_body->setReadOnly(true);
    m_status->setText(saveAsDraft ? QStringLiteral("下書きを保存中...") : QStringLiteral("送信中..."));
    emit submitRequested(draft(), saveAsDraft);
}

void ComposeDialog::submissionFinished(bool savedAsDraft, const QString &error)
{
    m_busy = false;
    m_send->setEnabled(true); m_save->setEnabled(true); m_cancel->setEnabled(true);
    for (auto *edit : findChildren<QLineEdit *>()) edit->setEnabled(true);
    m_body->setReadOnly(false);
    if (!error.isEmpty()) { m_status->setText(error + QStringLiteral("\n自動再試行は行っていません。送信の場合、再試行前にサーバ側も確認してください。")); return; }
    if (savedAsDraft) m_status->setText(QStringLiteral("下書きを保存しました。"));
    else accept();
}

void ComposeDialog::reject()
{
    if (!m_busy) QDialog::reject();
}
} // namespace smoothmail
