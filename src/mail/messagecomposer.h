#pragma once
#include "core/messagedocument.h"
#include "core/threading.h"

namespace smoothmail {
struct ComposeData {
    QString from;
    QString to;
    QString cc;
    QString bcc;
    QString subject;
    QString body;
    QString replySubject;
    ReplyHeaders reply;
};
struct ComposedMessage {
    QByteArray transportSource; // Never contains a Bcc header.
    QByteArray storageSource;   // Draft copy may contain Bcc.
    QString from;
    QStringList to, cc, bcc;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
ComposeData replyDraft(const QString &from, const MessageDocument &parent);
ComposedMessage composeMessage(const ComposeData &draft, bool saveAsDraft = false);
} // namespace smoothmail
