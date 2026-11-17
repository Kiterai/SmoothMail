#pragma once
#include "messagedocument.h"
#include <QMetaType>

namespace smoothmail {
// RFC 5258: \NonExistent implies \NoSelect, even if the server omits it.
inline bool isSelectableFolder(const QList<QByteArray> &flags)
{
    for (const auto &flag : flags)
        if (flag.compare("\\Noselect", Qt::CaseInsensitive) == 0
            || flag.compare("\\NonExistent", Qt::CaseInsensitive) == 0) return false;
    return true;
}
struct MailFolder {
    QString name;
    QChar separator;
    QList<QByteArray> flags;
    qint64 unread = 0;
};
struct MailRecord {
    QString accountId;
    QString folder;
    qint64 uidValidity = 0;
    qint64 uid = 0;
    bool seen = false;
    bool important = false;
    MessageDocument document;
    QString storageFolder;
};
}
Q_DECLARE_METATYPE(QList<smoothmail::MailFolder>)
Q_DECLARE_METATYPE(QList<smoothmail::MailRecord>)
Q_DECLARE_METATYPE(smoothmail::MailRecord)
