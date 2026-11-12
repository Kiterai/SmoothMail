#include "threading.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <numeric>

namespace smoothmail {
bool validMessageId(const QByteArray &id)
{
    // RFC 5322 msg-id, without the enclosing angle brackets. Deliberately
    // exclude obsolete syntax and folding; never permit header injection.
    static const QString atom = QStringLiteral("[A-Za-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\\.[A-Za-z0-9!#$%&'*+/=?^_`{|}~-]+)*");
    static const QString quoted = QStringLiteral(R"msg("(?:[\x21\x23-\x5b\x5d-\x7e]|\\[\x20-\x7e])*")msg");
    static const QString literal = QStringLiteral(R"msg(\[(?:[\x21-\x5a\x5e-\x7e]|\\[\x20-\x7e])*\])msg");
    static const QRegularExpression pattern(QStringLiteral("\\A(?:%1|%2)@(?:%1|%3)\\z").arg(atom, quoted, literal));
    return !id.isEmpty() && id.size() <= 996 && pattern.match(QString::fromLatin1(id)).hasMatch();
}

QList<int> threadParents(const QList<ThreadMessage> &messages)
{
    QHash<QString, QHash<QByteArray, QList<int>>> byAccount;
    for (qsizetype i = 0; i < messages.size(); ++i) {
        if (validMessageId(messages.at(i).messageId)) byAccount[messages.at(i).accountId][messages.at(i).messageId].append(int(i));
    }
    QList<int> parents(messages.size(), -1);
    QList<int> order(messages.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int left, int right) {
        const auto &a = messages.at(left);
        const auto &b = messages.at(right);
        if (a.accountId != b.accountId) return a.accountId < b.accountId;
        if (a.stableKey != b.stableKey) return a.stableKey < b.stableKey;
        return left < right;
    });
    for (const int child : order) {
        const auto &message = messages.at(child);
        const auto &index = byAccount[message.accountId];
        const auto safe = [&](int candidate) {
            if (candidate < 0 || candidate == child) return false;
            for (int ancestor = candidate; ancestor >= 0; ancestor = parents.at(ancestor)) {
                if (ancestor == child) return false;
            }
            return true;
        };
        QSet<int> direct;
        bool ambiguous = false;
        for (const auto &id : message.inReplyTo) {
            if (!validMessageId(id)) continue;
            const auto found = index.value(id);
            if (found.size() > 1) ambiguous = true;
            if (found.size() == 1) direct.insert(found.first());
        }
        if (!ambiguous && direct.size() == 1 && safe(*direct.begin())) parents[child] = *direct.begin();
        if (parents.at(child) >= 0) continue;
        for (auto ref = message.references.crbegin(); ref != message.references.crend(); ++ref) {
            if (!validMessageId(*ref)) continue;
            const auto found = index.value(*ref);
            if (found.size() == 1 && safe(found.first())) { parents[child] = found.first(); break; }
        }
    }
    return parents;
}

QList<int> compactThreadParents(const QList<int> &parents)
{
    QList<int> childCounts(parents.size(), 0);
    for (const auto parent : parents) {
        if (parent >= 0 && parent < parents.size()) ++childCounts[parent];
    }
    auto result = parents;
    for (qsizetype i = 0; i < parents.size(); ++i) {
        int parent = parents.at(i);
        QSet<int> visited;
        while (parent >= 0 && parent < parents.size() && parents.at(parent) >= 0 && childCounts.at(parent) == 1) {
            if (visited.contains(parent)) { parent = -1; break; }
            visited.insert(parent);
            parent = parents.at(parent);
        }
        result[i] = parent >= 0 && parent < parents.size() && parent != i ? parent : -1;
    }
    return result;
}

ReplyHeaders replyHeaders(const QByteArray &parentId, const QList<QByteArray> &references, const QList<QByteArray> &inReplyTo)
{
    ReplyHeaders result;
    for (const auto &id : references) if (validMessageId(id)) result.references.append(id);
    if (references.isEmpty() && inReplyTo.size() == 1 && validMessageId(inReplyTo.first())) {
        result.references.append(inReplyTo.first());
    }
    if (validMessageId(parentId)) {
        result.inReplyTo = parentId;
        result.references.append(parentId);
    }
    return result;
}

QByteArray newMessageId()
{
    return QUuid::createUuid().toByteArray(QUuid::WithoutBraces) + "@smoothmail.invalid";
}
} // namespace smoothmail
