#include "core/threading.h"
#include <QtTest>
using namespace smoothmail;

class ThreadingTest final : public QObject
{
    Q_OBJECT
private slots:
    void validatesIdentifiers()
    {
        for (const auto &id : {QByteArray("message@example.test"), QByteArray("a.b@[127.0.0.1]"), QByteArray("\"a\\ b\"@example.test")}) {
            QVERIFY2(validMessageId(id), id.constData());
        }
        for (const auto &id : {QByteArray(""), QByteArray("a"), QByteArray("<a@b>"), QByteArray("a..b@c"), QByteArray("a@b\r\nBcc: hidden@x")}) {
            QVERIFY(!validMessageId(id));
        }
        const auto first = newMessageId();
        QVERIFY(validMessageId(first));
        QVERIFY(first != newMessageId());
    }
    void respectsAccountsAndAmbiguity()
    {
        QList<ThreadMessage> rows{
            {"a", "1", "root@test", {}, {}},
            {"a", "2", "child@test", {"root@test"}, {}},
            {"b", "3", "other@test", {"root@test"}, {}},
            {"a", "4", "duplicate@test", {}, {}},
            {"a", "5", "duplicate@test", {}, {}},
            {"a", "6", "leaf@test", {"duplicate@test"}, {"root@test", "child@test"}}};
        QCOMPARE(threadParents(rows), QList<int>({-1, 0, -1, -1, -1, 1}));
    }
    void avoidsSelfReferencesAndCycles()
    {
        QList<ThreadMessage> rows{
            {"a", "1", "a@test", {"b@test"}, {}},
            {"a", "2", "b@test", {"a@test"}, {}},
            {"a", "3", "c@test", {"c@test"}, {}}};
        const auto parents = threadParents(rows);
        QCOMPARE(parents, QList<int>({1, -1, -1}));
    }
    void compactsOnlyLinearChains()
    {
        QCOMPARE(compactThreadParents({-1, 0, 1, 2, 3, 3}), QList<int>({-1, 0, 0, 0, 3, 3}));
    }
    void buildsReplyHeaders()
    {
        auto reply = replyHeaders("parent@test", {"root@test"}, {"ignored@test"});
        QCOMPARE(reply.inReplyTo, QByteArray("parent@test"));
        QCOMPARE(reply.references, QList<QByteArray>({"root@test", "parent@test"}));
        reply = replyHeaders("parent@test", {}, {"root@test"});
        QCOMPARE(reply.references, QList<QByteArray>({"root@test", "parent@test"}));
        reply = replyHeaders({}, {}, {"a@test", "b@test"});
        QVERIFY(reply.inReplyTo.isEmpty());
        QVERIFY(reply.references.isEmpty());
        reply = replyHeaders({}, {"root@test", "invalid"}, {});
        QVERIFY(reply.inReplyTo.isEmpty());
        QCOMPARE(reply.references, QList<QByteArray>({"root@test"}));
    }
};
QTEST_GUILESS_MAIN(ThreadingTest)
#include "tst_threading.moc"
