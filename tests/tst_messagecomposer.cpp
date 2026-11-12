#include "mail/messagecomposer.h"
#include "mail/mimeparser.h"
#include <QtTest>
using namespace smoothmail;
class MessageComposerTest final : public QObject
{
    Q_OBJECT
private slots:
    void composesUtf8AndHidesBcc()
    {
        ComposeData draft;
        draft.from = QStringLiteral("from@example.test"); draft.to = QStringLiteral("to@example.test");
        draft.bcc = QStringLiteral("hidden@example.test"); draft.subject = QStringLiteral("日本語の件名");
        draft.body = QStringLiteral("日本語本文\nsecond line");
        const auto result = composeMessage(draft);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(!result.transportSource.contains("hidden@example.test"));
        QVERIFY(!result.transportSource.toLower().contains("bcc:"));
        QVERIFY(result.storageSource.contains("hidden@example.test"));
        const auto parsed = MimeParser::parse(result.transportSource);
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.message.subject, draft.subject);
        QCOMPARE(parsed.message.parts.first().text.trimmed(), draft.body);
        QVERIFY(validMessageId(parsed.message.messageId));
        const auto second = MimeParser::parse(composeMessage(draft).transportSource);
        QVERIFY(second.message.messageId != parsed.message.messageId);
    }
    void replyUsesReplyToAndReferenceSemantics()
    {
        MessageDocument parent;
        parent.subject = QStringLiteral("hello"); parent.fromAddresses = {QStringLiteral("from@example.test")};
        parent.replyToAddresses = {QStringLiteral("reply1@example.test"), QStringLiteral("reply2@example.test")};
        parent.messageId = "parent@example.test"; parent.references = {"root@example.test"};
        auto draft = replyDraft(QStringLiteral("me@example.test"), parent);
        QCOMPARE(draft.to, QStringLiteral("reply1@example.test, reply2@example.test"));
        QCOMPARE(draft.subject, QStringLiteral("Re: hello"));
        const auto encoded = composeMessage(draft);
        QVERIFY(encoded.ok());
        const auto parsed = MimeParser::parse(encoded.transportSource);
        QCOMPARE(parsed.message.inReplyTo, QList<QByteArray>({"parent@example.test"}));
        QCOMPARE(parsed.message.references, QList<QByteArray>({"root@example.test", "parent@example.test"}));
        parent.replyToAddresses.clear(); parent.messageId.clear();
        draft = replyDraft(QStringLiteral("me@example.test"), parent);
        QCOMPARE(draft.to, QStringLiteral("from@example.test"));
        QVERIFY(draft.reply.inReplyTo.isEmpty());
    }
    void rejectsInjectionAndSupportsEmptyDraft()
    {
        ComposeData draft;
        draft.from = QStringLiteral("from@example.test");
        QVERIFY(!composeMessage(draft).ok());
        QVERIFY(composeMessage(draft, true).ok());
        draft.to = QStringLiteral("to@example.test\r\nBcc: hidden@example.test");
        QVERIFY(!composeMessage(draft).ok());
        draft.to = QStringLiteral("to@example.test"); draft.subject = QStringLiteral("hello\r\nBcc: hidden@example.test");
        QVERIFY(!composeMessage(draft).ok());
    }
};
QTEST_GUILESS_MAIN(MessageComposerTest)
#include "tst_messagecomposer.moc"
