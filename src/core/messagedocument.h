#pragma once

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

struct MessageDocument
{
    QString subject;
    QString senderName;
    QString senderAddress;
    QStringList recipients;
    QString headers;
    QList<MessagePart> parts;
};

} // namespace smoothmail
