#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

namespace smoothmail {
bool validMessageId(const QByteArray &id);
struct ThreadMessage {
    QString accountId;
    QString stableKey;
    QByteArray messageId;
    QList<QByteArray> inReplyTo;
    QList<QByteArray> references;
};
// Parent index in the original input, or -1. No subject-based inference.
QList<int> threadParents(const QList<ThreadMessage> &messages);
QList<int> compactThreadParents(const QList<int> &parents);
struct ReplyHeaders {
    QByteArray inReplyTo;
    QList<QByteArray> references;
};
ReplyHeaders replyHeaders(const QByteArray &parentId, const QList<QByteArray> &references,
                          const QList<QByteArray> &inReplyTo);
QByteArray newMessageId();
} // namespace smoothmail
