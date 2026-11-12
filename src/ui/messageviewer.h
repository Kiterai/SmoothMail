#pragma once

#include "core/messagedocument.h"

#include <QUrl>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QToolButton;
class QListWidget;

namespace smoothmail {

class SafeBodyView;

class MessageViewer final : public QWidget
{
    Q_OBJECT

public:
    explicit MessageViewer(QWidget *parent = nullptr);
    void displayMessage(const MessageDocument &message);
    void clear();

signals:
    void externalLinkRequested(const QUrl &url);
    void attachmentRequested(const QString &partId);

private:
    QLabel *m_summary;
    QListWidget *m_attachments;
    QToolButton *m_headerToggle;
    QPlainTextEdit *m_headers;
    SafeBodyView *m_body;
};

} // namespace smoothmail
