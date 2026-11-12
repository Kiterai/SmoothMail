#pragma once

#include "core/messagedocument.h"

#include <QUrl>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QToolButton;

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

private:
    QLabel *m_summary;
    QToolButton *m_headerToggle;
    QPlainTextEdit *m_headers;
    SafeBodyView *m_body;
};

} // namespace smoothmail
