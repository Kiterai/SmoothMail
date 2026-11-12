#pragma once

#include <QTextBrowser>

namespace smoothmail {

class SafeBodyView final : public QTextBrowser
{
    Q_OBJECT

public:
    explicit SafeBodyView(QWidget *parent = nullptr);
    void displayText(const QString &text);
    static bool isAllowedExternalUrl(const QUrl &url);

signals:
    void externalLinkRequested(const QUrl &url);

protected:
    QVariant loadResource(int type, const QUrl &name) override;
};

} // namespace smoothmail
