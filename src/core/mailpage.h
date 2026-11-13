#pragma once
#include "mailrecord.h"
namespace smoothmail {
inline constexpr int MailPageSize = 100;
struct MailQuery {
    QString accountId, folder, text;
    int page = 1, sortColumn = 6;
    bool importantOnly = false, unreadOnly = false;
    Qt::SortOrder order = Qt::DescendingOrder;
    quint64 generation = 0;
    qint64 revealUid = 0, revealValidity = 0;
    int pageSize = MailPageSize;
};
struct MailPage {
    MailQuery query;
    qint64 total = 0;
    QList<MailRecord> messages;
    QString error;
};
}
Q_DECLARE_METATYPE(smoothmail::MailQuery)
Q_DECLARE_METATYPE(smoothmail::MailPage)
