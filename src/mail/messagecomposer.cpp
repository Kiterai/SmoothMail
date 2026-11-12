#include "messagecomposer.h"
#include "mimeparser.h"
#include <KMime/Message>
#include <KMime/Headers>
#include <QDateTime>

namespace smoothmail {
namespace {
bool addresses(const QString &field, QStringList &result)
{
    if (field.trimmed().isEmpty()) return true;
    for (const auto &item : field.split(u',')) {
        const auto address = item.trimmed();
        // UI fields accept comma-separated addr-spec values, not arbitrary
        // headers. KMime generates all encoding and header syntax afterward.
        if (!validMessageId(address.toUtf8())) return false;
        result.append(address);
    }
    return true;
}
}

ComposeData replyDraft(const QString &from, const MessageDocument &parent)
{
    ComposeData result;
    result.from = from;
    result.to = (parent.hasReplyTo || !parent.replyToAddresses.isEmpty() ? parent.replyToAddresses : parent.fromAddresses).join(QStringLiteral(", "));
    result.subject = QStringLiteral("Re: ") + parent.subject;
    result.replySubject = parent.subject;
    result.reply = replyHeaders(parent.messageId, parent.references, parent.inReplyTo);
    return result;
}

ComposedMessage composeMessage(const ComposeData &draft, bool saveAsDraft)
{
    ComposedMessage result;
    QStringList from;
    if (!addresses(draft.from, from) || from.size() != 1 || !addresses(draft.to, result.to)
        || !addresses(draft.cc, result.cc) || !addresses(draft.bcc, result.bcc)) {
        result.error = QStringLiteral("アドレスはカンマ区切りで正しく入力してください。");
        return result;
    }
    if (!saveAsDraft && result.to.isEmpty() && result.cc.isEmpty() && result.bcc.isEmpty()) {
        result.error = QStringLiteral("宛先を入力してください。");
        return result;
    }
    for (const auto c : draft.subject) {
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) {
            result.error = QStringLiteral("件名に制御文字は使用できません。");
            return result;
        }
    }
    if (draft.body.size() > MimeParser::MaxRawBytes || draft.subject.size() > 32768) {
        result.error = QStringLiteral("メールがサイズ上限を超えています。");
        return result;
    }
    if (!draft.reply.inReplyTo.isEmpty() && !validMessageId(draft.reply.inReplyTo)) {
        result.error = QStringLiteral("返信元の識別子が不正です。");
        return result;
    }
    KMime::Message message;
    result.from = from.first();
    message.from()->addAddress(result.from.toUtf8());
    for (const auto &to : result.to) message.to()->addAddress(to.toUtf8());
    for (const auto &cc : result.cc) message.cc()->addAddress(cc.toUtf8());
    message.subject()->setRFC2047Charset("utf-8");
    message.subject()->fromUnicodeString(draft.subject);
    message.messageID()->setIdentifier(newMessageId());
    message.date()->setDateTime(QDateTime::currentDateTime());
    auto version = std::make_unique<KMime::Headers::MIMEVersion>();
    version->from7BitString("1.0");
    message.setHeader(std::move(version));
    message.contentType()->setMimeType("text/plain");
    message.contentType()->setCharset("utf-8");
    message.contentTransferEncoding()->setEncoding(KMime::Headers::CEbase64);
    auto body = draft.body;
    body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    body.replace(u'\r', u'\n');
    message.fromUnicodeString(body);
    if (!draft.reply.inReplyTo.isEmpty()) message.inReplyTo()->appendIdentifier(draft.reply.inReplyTo);
    for (const auto &id : draft.reply.references) {
        if (!validMessageId(id)) { result.error = QStringLiteral("返信参照識別子が不正です。"); return result; }
        message.references()->appendIdentifier(id);
    }
    message.assemble();
    result.transportSource = message.encodedContent(KMime::NewlineType::CRLF);
    for (const auto &bcc : result.bcc) message.bcc()->addAddress(bcc.toUtf8());
    message.assemble();
    result.storageSource = message.encodedContent(KMime::NewlineType::CRLF);
    if (result.storageSource.size() > MimeParser::MaxRawBytes) {
        result.transportSource.clear(); result.storageSource.clear();
        result.error = QStringLiteral("エンコード済みメールがサイズ上限を超えています。");
    }
    return result;
}
} // namespace smoothmail
