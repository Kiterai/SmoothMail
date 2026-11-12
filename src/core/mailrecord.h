#pragma once
#include "messagedocument.h"
#include <QMetaType>

namespace smoothmail {
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
};
}
Q_DECLARE_METATYPE(QList<smoothmail::MailFolder>)
Q_DECLARE_METATYPE(QList<smoothmail::MailRecord>)
Q_DECLARE_METATYPE(smoothmail::MailRecord)
