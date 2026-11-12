#include "mimeparser.h"

#include <KMime/Content>
#include <KMime/Headers>
#include <KMime/Message>
#include <KMime/Util>

#include <utility>

namespace smoothmail {
namespace {

bool collectParts(const KMime::Content &content, const QString &id, int depth,
                  int &partCount, MessageDocument &document)
{
    if (depth > MimeParser::MaxPartDepth || ++partCount > MimeParser::MaxParts) {
        return false;
    }
    const auto *type = content.contentType();
    const auto *disposition = content.contentDisposition();
    const auto mediaType = type ? QString::fromLatin1(type->mimeType()) : QStringLiteral("text/plain");
    QString fileName;
    if (disposition) {
        fileName = disposition->filename();
    }
    if (fileName.isEmpty() && type) {
        fileName = type->name();
    }
    const bool explicitAttachment = !fileName.isEmpty()
        || (disposition && disposition->disposition() == KMime::Headers::CDattachment);
    if (explicitAttachment) {
        document.attachments.append({id, fileName, mediaType});
        return true;
    }

    const auto children = content.contents();
    if (!children.empty()) {
        int index = 1;
        for (const auto *child : children) {
            const auto childId = id + u'.' + QString::number(index++);
            if (!collectParts(*child, childId, depth + 1, partCount, document)) {
                return false;
            }
        }
    } else if (!type || type->isText()) {
        // Includes text/html: it remains source text, never rendered here.
        document.parts.append({mediaType, content.decodedText()});
    } else {
        // Binary inline parts are attachments, never decoded as body text.
        document.attachments.append({id, fileName, mediaType});
    }
    return true;
}

} // namespace

MessageParseResult MimeParser::parse(const QByteArray &rawMessage)
{
    if (rawMessage.isEmpty()) {
        return {{}, QStringLiteral("メール原文が空です。")};
    }
    if (rawMessage.size() > MaxRawBytes) {
        return {{}, QStringLiteral("メール原文がサイズ上限を超えています。")};
    }

    KMime::Message parsed;
    parsed.setContent(KMime::CRLFtoLF(rawMessage));
    parsed.parse();
    const auto &message = std::as_const(parsed);
    MessageDocument document;
    document.rawSource = rawMessage;
    document.headers = QString::fromUtf8(message.head());
    if (const auto *subject = message.subject()) {
        document.subject = subject->asUnicodeString();
    }
    if (const auto *from = message.from()) {
        for (const auto &address : from->addresses()) document.fromAddresses.append(QString::fromUtf8(address));
        const auto mailboxes = from->mailboxes();
        if (!mailboxes.isEmpty()) {
            document.senderName = mailboxes.first().name();
            document.senderAddress = QString::fromUtf8(mailboxes.first().address());
        }
    }
    if (const auto *reply = message.replyTo()) {
        for (const auto &address : reply->addresses()) document.replyToAddresses.append(QString::fromUtf8(address));
    }
    if (const auto *date = message.date()) document.date = date->dateTime();
    if (const auto *to = message.to()) {
        const auto addresses = to->addresses();
        for (const auto &address : addresses) {
            document.recipients.append(QString::fromUtf8(address));
        }
    }
    if (const auto *id = message.messageID()) {
        document.messageId = id->identifier();
    }
    if (const auto *parent = message.inReplyTo()) {
        document.inReplyTo = parent->identifiers();
    }
    if (const auto *references = message.references()) {
        document.references = references->identifiers();
    }
    int partCount = 0;
    if (!collectParts(message, QStringLiteral("1"), 0, partCount, document)) {
        return {{}, QStringLiteral("メールのMIME構造が上限を超えています。")};
    }
    return {std::move(document), {}};
}

AttachmentResult MimeParser::extractAttachment(const QByteArray &raw, const QString &id)
{
    const auto parsed = parse(raw);
    if (!parsed.ok()) return {{}, {}, parsed.error};
    QString name;
    bool attachment = false;
    for (const auto &item : parsed.message.attachments) {
        if (item.partId == id) { attachment = true; name = item.fileName; break; }
    }
    if (!attachment) return {{}, {}, QStringLiteral("添付ファイルが見つかりません。")};
    KMime::Message message;
    message.setContent(KMime::CRLFtoLF(raw)); message.parse();
    const KMime::Content *part = &message;
    const auto components = id.split(u'.');
    if (components.isEmpty() || components.first() != QStringLiteral("1")) return {{}, {}, QStringLiteral("添付識別子が不正です。")};
    for (qsizetype i = 1; i < components.size(); ++i) {
        bool ok = false;
        const auto index = components.at(i).toInt(&ok);
        const auto children = part->contents();
        if (!ok || index < 1 || index > int(children.size()) || QString::number(index) != components.at(i)) {
            return {{}, {}, QStringLiteral("添付識別子が不正です。")};
        }
        part = children[size_t(index - 1)];
    }
    const auto encapsulated = part->bodyIsMessage() ? part->bodyAsMessage() : nullptr;
    const auto data = encapsulated ? encapsulated->encodedContent(KMime::NewlineType::CRLF) : part->decodedBody();
    if (data.size() > MaxRawBytes) return {{}, {}, QStringLiteral("添付データがサイズ上限を超えています。")};
    return {data, name, {}};
}

} // namespace smoothmail
