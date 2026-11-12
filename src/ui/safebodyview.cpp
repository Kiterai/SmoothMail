#include "safebodyview.h"

#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QUrl>

namespace smoothmail {
namespace {

QString trimUrlPunctuation(QString candidate)
{
    while (!candidate.isEmpty()) {
        const auto last = candidate.back();
        const bool punctuation = QStringLiteral(".,;:!?").contains(last);
        const bool unmatchedParenthesis = last == u')' && candidate.count(u')') > candidate.count(u'(');
        const bool unmatchedBracket = last == u']' && candidate.count(u']') > candidate.count(u'[');
        if (!punctuation && !unmatchedParenthesis && !unmatchedBracket) {
            break;
        }
        candidate.chop(1);
    }
    return candidate;
}

} // namespace

SafeBodyView::SafeBodyView(QWidget *parent)
    : QTextBrowser(parent)
{
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setAcceptRichText(false);
    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (isAllowedExternalUrl(url)) {
            emit externalLinkRequested(url);
        }
    });
}

bool SafeBodyView::isAllowedExternalUrl(const QUrl &url)
{
    const auto scheme = url.scheme().toLower();
    if (!url.isValid() || url.isRelative() || url.host().isEmpty()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        return false;
    }
    // Credentials embedded in links can obscure the actual destination.
    if (!url.userInfo().isEmpty()) {
        return false;
    }
    const auto decoded = QUrl::fromPercentEncoding(url.toEncoded());
    for (const auto character : decoded) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
            return false;
        }
    }
    return true;
}

void SafeBodyView::displayText(const QString &text)
{
    clear();
    setCurrentCharFormat(QTextCharFormat());
    setPlainText(text);
    // QTextDocument normalizes CRLF and paragraph separators. Match its actual
    // text so cursor positions never refer to the unnormalized input offsets.
    const auto displayed = document()->toPlainText();
    static const QRegularExpression urlPattern(
        QStringLiteral(R"((?<![A-Za-z0-9_])https?://[^\s<>"']+)"),
        QRegularExpression::CaseInsensitiveOption);
    auto matches = urlPattern.globalMatch(displayed);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto candidate = trimUrlPunctuation(match.captured());
        const QUrl url(candidate, QUrl::StrictMode);
        if (!isAllowedExternalUrl(url)) {
            continue;
        }

        const auto start = static_cast<int>(match.capturedStart());
        const auto length = static_cast<int>(candidate.size());
        QTextCursor cursor(document());
        cursor.setPosition(start);
        cursor.setPosition(start + length, QTextCursor::KeepAnchor);
        QTextCharFormat link;
        link.setAnchor(true);
        link.setAnchorHref(candidate);
        link.setForeground(Qt::blue);
        link.setFontUnderline(true);
        cursor.setCharFormat(link);

        // Work in UTF-16 ranges, matching QTextCursor positions, including pairs.
        int offset = 0;
        while (offset < length) {
            if (candidate.at(offset).unicode() <= 0x7f) {
                ++offset;
                continue;
            }
            const int first = offset;
            while (offset < length && candidate.at(offset).unicode() > 0x7f) {
                ++offset;
            }
            cursor.setPosition(start + first);
            cursor.setPosition(start + offset, QTextCursor::KeepAnchor);
            auto warning = link;
            warning.setForeground(Qt::red);
            warning.setFontWeight(QFont::Bold);
            cursor.setCharFormat(warning);
        }
    }
    moveCursor(QTextCursor::Start);
}

QVariant SafeBodyView::loadResource(int, const QUrl &)
{
    // No images, CSS, file reads or network loads from message content.
    return {};
}

} // namespace smoothmail
