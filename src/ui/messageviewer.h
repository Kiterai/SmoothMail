#pragma once

#include "core/messagedocument.h"

#include <QUrl>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QToolButton;
class QListWidget;
class QLineEdit;

namespace smoothmail {

class SafeBodyView;

class MessageViewer final : public QWidget
{
    Q_OBJECT

public:
    explicit MessageViewer(QWidget *parent = nullptr);
    void displayMessage(const MessageDocument &message);
    void clear();
    void showSearch();
    void showLoading();
    void showFailure();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;

signals:
    void externalLinkRequested(const QUrl &url);
    void attachmentRequested(const QString &partId);

private:
    QLabel *m_summary;
    QListWidget *m_attachments;
    QToolButton *m_headerToggle;
    QPlainTextEdit *m_headers;
    SafeBodyView *m_body;
    QWidget *m_search = nullptr;
    QLineEdit *m_searchText = nullptr;
    QLabel *m_searchCount;
    int m_position = -1, m_current = 0, m_matches = 0;
    QList<int> m_matchPositions;
    bool m_hasMessage = false;
    int m_headerHeight = 150;
    void countMatches();
    void findMatch(bool previous);
    void highlightMatches();
    void centerCurrentMatch();
    void hideSearch();
};

} // namespace smoothmail
