#include "ui/messageviewer.h"
#include "ui/safebodyview.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QtTest>
#include <QListWidget>
#include <QLineEdit>

namespace {

QTextCharFormat formatAt(QTextDocument *document, int position)
{
    QTextCursor cursor(document);
    cursor.setPosition(position);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    return cursor.charFormat();
}

} // namespace

class MessageViewerTest final : public QObject
{
    Q_OBJECT

private slots:
    void bodySearchWrapsInBothDirectionsAndResets()
    {
        smoothmail::MessageViewer viewer; smoothmail::MessageDocument document;
        document.parts = {{QStringLiteral("text/plain"), QStringLiteral("ababa ABA")}};
        viewer.displayMessage(document); viewer.show(); viewer.showSearch();
        auto *input = viewer.findChild<QLineEdit *>(QStringLiteral("bodySearchText")); auto *count = viewer.findChild<QLabel *>(QStringLiteral("bodySearchCount")); auto *body = viewer.findChild<smoothmail::SafeBodyView *>();
        auto *bar = viewer.findChild<QWidget *>(QStringLiteral("bodySearchBar")); QVERIFY(bar->isVisible());
        input->setText(QStringLiteral("aba")); QCOMPARE(count->text(), QStringLiteral("1 / 2 件")); QCOMPARE(body->textCursor().selectionStart(), 0);
        QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier); QCOMPARE(count->text(), QStringLiteral("2 / 2 件")); QCOMPARE(body->textCursor().selectionStart(), 6);
        QTest::keyClick(input, Qt::Key_Return); QCOMPARE(count->text(), QStringLiteral("1 / 2 件")); QCOMPARE(body->textCursor().selectionStart(), 0);
        QTest::keyClick(input, Qt::Key_Return); QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier); QCOMPARE(body->textCursor().selectionStart(), 0);
        input->setText(QStringLiteral("missing")); QCOMPARE(count->text(), QStringLiteral("0 / 0 件")); QTest::keyClick(input, Qt::Key_Return);
        QTest::keyClick(input, Qt::Key_Escape); QVERIFY(bar->isHidden()); viewer.showSearch(); QVERIFY(bar->isVisible());
        document.parts = {{QStringLiteral("text/plain"), QStringLiteral("missing")}}; viewer.displayMessage(document); QCOMPARE(count->text(), QStringLiteral("1 / 1 件"));
        input->clear(); QCOMPARE(count->text(), QStringLiteral("0 / 0 件"));
    }
    void htmlRemainsLiteral()
    {
        smoothmail::SafeBodyView view;
        const QString text = QStringLiteral("<h1>本文</h1><img src='https://tracker.example/pixel'>"
                                            "<script>alert(1)</script>");
        view.displayText(text);
        QCOMPARE(view.toPlainText(), text);
        QVERIFY(!view.document()->toHtml().contains(QStringLiteral("<img src=")));
        QVERIFY(!view.openLinks());
        QVERIFY(!view.openExternalLinks());
    }

    void decoratesOnlyUrls()
    {
        smoothmail::SafeBodyView view;
        const QString text = QStringLiteral("日本語 https://example.com/道/😀?q=1 fin");
        view.displayText(text);
        const int start = static_cast<int>(text.indexOf(QStringLiteral("https://")));
        const int unicode = static_cast<int>(text.indexOf(QStringLiteral("道")));
        const int emoji = static_cast<int>(text.indexOf(QStringLiteral("😀")));
        const auto plain = formatAt(view.document(), 0);
        QVERIFY(!plain.isAnchor());
        QVERIFY(plain.foreground().color() != QColor(Qt::red));
        const auto ascii = formatAt(view.document(), start);
        QVERIFY(ascii.isAnchor());
        QCOMPARE(ascii.foreground().color(), QColor(Qt::blue));
        const auto nonAscii = formatAt(view.document(), unicode);
        QVERIFY(nonAscii.isAnchor());
        QCOMPARE(nonAscii.foreground().color(), QColor(Qt::red));
        QCOMPARE(nonAscii.fontWeight(), int(QFont::Bold));
        QCOMPARE(formatAt(view.document(), emoji).foreground().color(), QColor(Qt::red));
        QVERIFY(!formatAt(view.document(), static_cast<int>(text.size()) - 1).isAnchor());
        QCOMPARE(view.toPlainText(), text);
    }

    void handlesPunctuationAndCase()
    {
        smoothmail::SafeBodyView view;
        const QString text = QStringLiteral("(HTTPS://example.com/a(b)).");
        view.displayText(text);
        const auto link = formatAt(view.document(), 1);
        QCOMPARE(link.anchorHref(), QStringLiteral("HTTPS://example.com/a(b)"));
        QVERIFY(!formatAt(view.document(), static_cast<int>(text.size()) - 1).isAnchor());
        QVERIFY(!formatAt(view.document(), static_cast<int>(text.size()) - 2).isAnchor());
    }

    void handlesWindowsLineEndings()
    {
        smoothmail::SafeBodyView view;
        view.displayText(QStringLiteral("先頭\r\nhttps://example.com/道\r\n末尾"));
        const auto displayed = view.toPlainText();
        QCOMPARE(displayed, QStringLiteral("先頭\nhttps://example.com/道\n末尾"));
        const int start = static_cast<int>(displayed.indexOf(QStringLiteral("https://")));
        const int unicode = static_cast<int>(displayed.indexOf(QStringLiteral("道")));
        QVERIFY(formatAt(view.document(), start).isAnchor());
        QCOMPARE(formatAt(view.document(), unicode).foreground().color(), QColor(Qt::red));
    }

    void refusesResourceLoads()
    {
        smoothmail::SafeBodyView view;
        QVERIFY(!view.document()->resource(QTextDocument::ImageResource,
                                           QUrl(QStringLiteral("https://example.com/pixel"))).isValid());
        QVERIFY(!view.document()->resource(QTextDocument::ImageResource,
                                           QUrl(QStringLiteral("file:///C:/secret.png"))).isValid());
    }

    void externalUrlPolicy_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("https") << QStringLiteral("https://example.com/path") << true;
        QTest::newRow("http") << QStringLiteral("http://example.com") << true;
        QTest::newRow("unicode") << QStringLiteral("https://example.com/日本語") << true;
        QTest::newRow("ipv6") << QStringLiteral("https://[::1]/") << true;
        QTest::newRow("file") << QStringLiteral("file:///C:/secret.txt") << false;
        QTest::newRow("javascript") << QStringLiteral("javascript:alert(1)") << false;
        QTest::newRow("data") << QStringLiteral("data:text/html,test") << false;
        QTest::newRow("mailto") << QStringLiteral("mailto:user@example.com") << false;
        QTest::newRow("relative") << QStringLiteral("/path") << false;
        QTest::newRow("network-path") << QStringLiteral("//example.com/path") << false;
        QTest::newRow("empty-host") << QStringLiteral("https:///path") << false;
        QTest::newRow("credentials") << QStringLiteral("https://trusted.example@evil.example/") << false;
        QTest::newRow("control") << QStringLiteral("https://example.com/%0d%0a") << false;
    }

    void externalUrlPolicy()
    {
        QFETCH(QString, text);
        QFETCH(bool, allowed);
        QCOMPARE(smoothmail::SafeBodyView::isAllowedExternalUrl(QUrl(text, QUrl::StrictMode)), allowed);
    }

    void rejectsUnsafeClicks()
    {
        smoothmail::SafeBodyView view;
        QSignalSpy spy(&view, &smoothmail::SafeBodyView::externalLinkRequested);
        view.anchorClicked(QUrl(QStringLiteral("file:///C:/secret.txt")));
        view.anchorClicked(QUrl(QStringLiteral("javascript:alert(1)")));
        QCOMPARE(spy.count(), 0);
        const QUrl safe(QStringLiteral("https://example.com/"));
        view.anchorClicked(safe);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toUrl(), safe);
    }

    void clearsOldFormatting()
    {
        smoothmail::SafeBodyView view;
        view.displayText(QStringLiteral("https://example.com/日本語"));
        view.displayText(QStringLiteral("通常の本文"));
        QVERIFY(!formatAt(view.document(), 0).isAnchor());
        QVERIFY(formatAt(view.document(), 0).foreground().color() != QColor(Qt::red));
    }

    void headerToggleResetsForEachMessage()
    {
        smoothmail::MessageViewer viewer;
        smoothmail::MessageDocument message;
        message.subject = QStringLiteral("<b>件名</b>");
        message.senderAddress = QStringLiteral("sender@example.com");
        message.recipients = {QStringLiteral("receiver@example.com")};
        message.headers = QStringLiteral("Subject: <b>件名</b>\nX-Test: value");
        message.parts = {{QStringLiteral("text/plain"), QStringLiteral("本文")},
                         {QStringLiteral("text/html"), QStringLiteral("<p>HTML</p>")}};
        viewer.displayMessage(message);
        auto *toggle = viewer.findChild<QToolButton *>(QStringLiteral("headerToggle"));
        auto *headers = viewer.findChild<QPlainTextEdit *>(QStringLiteral("messageHeaders"));
        auto *summary = viewer.findChild<QLabel *>(QStringLiteral("messageSummary"));
        auto *body = viewer.findChild<smoothmail::SafeBodyView *>(QStringLiteral("messageBody"));
        QVERIFY(toggle);
        QVERIFY(headers);
        QVERIFY(summary);
        QVERIFY(body);
        QCOMPARE(summary->textFormat(), Qt::PlainText);
        QVERIFY(summary->text().contains(message.subject));
        QVERIFY(!toggle->isChecked());
        QVERIFY(headers->isHidden());
        toggle->setChecked(true);
        QVERIFY(!headers->isHidden());
        QCOMPARE(headers->toPlainText(), message.headers);
        QCOMPARE(body->toPlainText(), QStringLiteral("本文\n\n<p>HTML</p>"));
        viewer.displayMessage(message);
        QVERIFY(!toggle->isChecked());
        QVERIFY(headers->isHidden());
        viewer.clear();
        QVERIFY(!toggle->isEnabled());
        QVERIFY(headers->toPlainText().isEmpty());
        QVERIFY(summary->text().isEmpty());
        QCOMPARE(body->toPlainText(), QStringLiteral("表示するメールはありません。"));
    }
    void attachmentsEmitOnlyPartIdentifiers()
    {
        smoothmail::MessageViewer viewer;
        smoothmail::MessageDocument document;
        document.attachments.append({QStringLiteral("1.2"), QStringLiteral("<not-html>.txt"), QStringLiteral("text/plain")});
        QSignalSpy requested(&viewer, &smoothmail::MessageViewer::attachmentRequested);
        viewer.displayMessage(document);
        auto *list = viewer.findChild<QListWidget *>(QStringLiteral("messageAttachments"));
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->text(), QStringLiteral("<not-html>.txt"));
        list->itemClicked(list->item(0));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.first().first().toString(), QStringLiteral("1.2"));
        viewer.clear();
        QCOMPARE(list->count(), 0);
        QVERIFY(list->isHidden());
    }
};

QTEST_MAIN(MessageViewerTest)
#include "tst_messageviewer.moc"
