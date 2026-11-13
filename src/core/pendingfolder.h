#pragma once
#include <QString>
namespace smoothmail {
struct PendingFolder {
    qint64 revision = 0;
    QString accountId, operation, source, destination;
    QChar separator;
};
}
