#pragma once
#include <QDateTime>
#include <QMetaType>
#include <QString>

namespace smoothmail {
struct DownloadEntry {
    QString id;
    QString accountId;
    QString folder;
    qint64 uidValidity = 0;
    qint64 uid = 0;
    QString subject;
    QString path;
    qint64 size = 0;
    QDateTime savedAt;
    QString mimeType;
};
}
Q_DECLARE_METATYPE(smoothmail::DownloadEntry)
Q_DECLARE_METATYPE(QList<smoothmail::DownloadEntry>)
