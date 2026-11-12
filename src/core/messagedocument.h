#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace smoothmail {

// Decoded values only. MIME parsing, storage and transport stay outside renderers.
struct MessagePart
{
    QString mediaType;
    QString text;
};

struct MessageAttachment
{
    QString partId;
    QString fileName;
    QString mediaType;
};

struct MessageDocument
{
    QByteArray rawSource;
    QByteArray messageId;
    QList<QByteArray> inReplyTo;
    QList<QByteArray> references;
    QString subject;
    QString senderName;
    QString senderAddress;
    QStringList recipients;
    QString headers;
    QList<MessagePart> parts;
    QList<MessageAttachment> attachments;
};

} // namespace smoothmail
