#include "downloadfile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace smoothmail {
QString safeDownloadName(const QString &suggested)
{
    QString name;
    for (const auto character : suggested.normalized(QString::NormalizationForm_C)) {
        if (character.category() == QChar::Other_Control || character.category() == QChar::Other_Format
            || QStringLiteral("<>:\"/\\|?*").contains(character)) name += u'_';
        else name += character;
    }
    name = name.trimmed();
    while (name.endsWith(u'.') || name.endsWith(u' ')) name.chop(1);
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")) name = QStringLiteral("attachment");
    static const QRegularExpression devices(QStringLiteral("^(CON|PRN|AUX|NUL|CONIN\\$|CONOUT\\$|COM[1-9¹²³]|LPT[1-9¹²³])$"), QRegularExpression::CaseInsensitiveOption);
    if (devices.match(name.section(u'.', 0, 0).trimmed()).hasMatch()) name.prepend(u'_');
    name = name.left(180);
    if (!name.isEmpty() && name.at(name.size() - 1).isHighSurrogate()) name.chop(1);
    while (name.endsWith(u'.') || name.endsWith(u' ')) name.chop(1);
    return name.isEmpty() ? QStringLiteral("attachment") : name;
}

DownloadFileResult saveDownloadFile(const QString &directory, const QString &suggested, const QByteArray &data)
{
    if (directory.isEmpty() || !QDir().mkpath(directory)) return {{}, QStringLiteral("Downloads保存先を作成できません。")};
    const QDir destination(QFileInfo(directory).absoluteFilePath());
    const auto name = safeDownloadName(suggested);
    const int dot = name.lastIndexOf(u'.');
    const auto stem = dot > 0 ? name.left(dot) : name;
    const auto extension = dot > 0 ? name.mid(dot) : QString{};
    for (int suffix = 0; suffix < 10000; ++suffix) {
        const auto leaf = suffix ? stem + QStringLiteral(" (%1)").arg(suffix) + extension : name;
        const auto path = destination.absoluteFilePath(leaf);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            const QFileInfo existing(path);
            if (existing.exists() || existing.isSymbolicLink()) continue;
            return {{}, file.errorString()};
        }
        if (file.write(data) != data.size() || !file.flush()) {
            const auto error = file.errorString();
            file.close(); file.remove();
            return {{}, error.isEmpty() ? QStringLiteral("添付ファイルを書き込めません。") : error};
        }
        file.close();
        return {path, {}};
    }
    return {{}, QStringLiteral("同名ファイルが多すぎるため保存できません。")};
}
}
