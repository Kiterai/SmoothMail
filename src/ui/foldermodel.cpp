#include "foldermodel.h"
#include <QMimeData>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

namespace smoothmail {
namespace {
const QString mime = QStringLiteral("application/x-smoothmail-account-order"), folderMime = QStringLiteral("application/x-smoothmail-folder"), mailMime = QStringLiteral("application/x-smoothmail-messages");
QString token() { static const auto value = QUuid::createUuid().toString(); return value; }
}
FolderModel::FolderModel(QObject *parent) : QStandardItemModel(parent)
{
    invisibleRootItem()->setDropEnabled(true);
}
QStringList FolderModel::mimeTypes() const { return {mime, folderMime, mailMime}; }
QMimeData *FolderModel::mimeData(const QModelIndexList &indexes) const
{
    auto *data = new QMimeData;
    if (indexes.size() != 1) return data;
    if (indexes.first().parent().isValid()) {
        const auto index = indexes.first();
        data->setData(folderMime, QJsonDocument(QJsonObject{{"token", token()}, {"account", index.data(Qt::UserRole).toString()}, {"folder", index.data(Qt::UserRole + 1).toString()}, {"separator", index.data(Qt::UserRole + 4).toString()}}).toJson(QJsonDocument::Compact)); return data;
    }
    const auto id = indexes.first().data(Qt::UserRole).toString();
    if (!id.isEmpty()) data->setData(mime, id.toUtf8());
    return data;
}
bool FolderModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    if (action == Qt::IgnoreAction) return true;
    if (action == Qt::CopyAction && parent.isValid() && (data->hasFormat(folderMime) || data->hasFormat(mailMime))) {
        const auto target = parent.siblingAtColumn(0); const auto id = target.data(Qt::UserRole).toString(), destination = target.data(Qt::UserRole + 1).toString();
        if (data->hasFormat(folderMime)) {
            const auto object = QJsonDocument::fromJson(data->data(folderMime)).object(); const auto source = object.value("folder").toString(), separator = object.value("separator").toString();
            if (object.value("token").toString() != token() || object.value("account").toString() != id || source.isEmpty() || separator.size() != 1 || source == destination || destination.startsWith(source + separator)) return false;
            emit folderMoveRequested(id, source, destination, separator.front()); return true;
        }
        if (destination.isEmpty() || !(flags(target) & Qt::ItemIsSelectable)) return false;
        const auto object = QJsonDocument::fromJson(data->data(mailMime)).object(); if (object.value("token").toString() != token()) return false;
        const auto rows = object.value("messages").toArray(); if (rows.isEmpty() || rows.size() > 1000) return false;
        QList<MailRecord> identities;
        for (const auto &row : rows) {
            const auto entry = row.toObject();
            MailRecord identity{id, entry.value("folder").toString(), entry.value("validity").toString().toLongLong(), entry.value("uid").toString().toLongLong()}; identity.storageFolder = entry.value("storage").toString();
            if (entry.value("account").toString() != id || identity.folder.isEmpty() || identity.uid <= 0 || identity.uidValidity <= 0) return false;
            identities.append(identity);
        }
        emit messageMoveBatchStarted();
        for (const auto &identity : identities) emit messageMoveRequested(identity, destination);
        emit messageMoveBatchFinished();
        return true;
    }
    // CopyAction is only the drag protocol: move here, without a second source deletion by QTreeView.
    if (action != Qt::CopyAction || parent.isValid() || column > 0 || !data->hasFormat(mime)) return false;
    const auto id = QString::fromUtf8(data->data(mime));
    int source = -1;
    for (int i = 0; i < rowCount(); ++i) if (item(i)->data(Qt::UserRole).toString() == id) source = i;
    if (source < 0) return false;
    if (row < 0) row = rowCount();
    if (row > rowCount()) return false;
    if (row > source) --row;
    if (row == source) return true;
    auto moved = takeRow(source);
    insertRow(row, moved);
    QStringList ids;
    for (int i = 0; i < rowCount(); ++i) ids.append(item(i)->data(Qt::UserRole).toString());
    emit accountOrderChanged(ids);
    return true;
}
QStringList MessageListModel::mimeTypes() const { return {mailMime}; }
QMimeData *MessageListModel::mimeData(const QModelIndexList &indexes) const
{
    auto *data = new QMimeData; QSet<int> selected; QJsonArray rows;
    for (const auto &index : indexes) { const int row = index.data(Qt::UserRole).toInt(); if (row < 0 || row >= records.size() || selected.contains(row)) continue; selected.insert(row); const auto &record = records.at(row); rows.append(QJsonObject{{"account", record.accountId}, {"folder", record.folder}, {"validity", QString::number(record.uidValidity)}, {"uid", QString::number(record.uid)}, {"storage", record.storageFolder}}); }
    data->setData(mailMime, QJsonDocument(QJsonObject{{"token", token()}, {"messages", rows}}).toJson(QJsonDocument::Compact)); return data;
}
}
