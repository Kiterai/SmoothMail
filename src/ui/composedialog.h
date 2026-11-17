#pragma once
#include "mime/messagecomposer.h"
#include <QDialog>
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;
namespace smoothmail {
class ComposeDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit ComposeDialog(ComposeData draft, QWidget *parent = nullptr, bool hasReplySource = false);
    ComposeData draft() const;
    void submissionFinished(bool savedAsDraft, const QString &error);
    void sourceNavigationFailed(const QString &error);
signals:
    void submitRequested(const smoothmail::ComposeData &draft, bool saveAsDraft);
    void replySourceRequested();
protected:
    void reject() override;
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void submit(bool saveAsDraft);
    ComposeData m_original;
    QLineEdit *m_subject, *m_from, *m_to, *m_cc, *m_bcc;
    QPlainTextEdit *m_body;
    QLabel *m_status, *m_reply;
    QPushButton *m_send, *m_save, *m_cancel;
    bool m_busy = false;
    bool m_hasReplySource = false, m_replyPressed = false;
};
}
