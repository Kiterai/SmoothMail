#pragma once
#include <QString>
#include <QByteArray>

namespace smoothmail {
QString safeDownloadName(const QString &suggested);
struct DownloadFileResult { QString path; QString error; };
DownloadFileResult saveDownloadFile(const QString &directory, const QString &suggested, const QByteArray &data);
}
