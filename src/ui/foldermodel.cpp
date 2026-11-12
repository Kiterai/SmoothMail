#include "foldermodel.h"
#include <QMimeData>

namespace smoothmail {
namespace { const QString mime = QStringLiteral("application/x-smoothmail-account-order"); }
FolderModel::FolderModel(QObject *parent) : QStandardItemModel(parent)
{
    invisibleRootItem()->setDropEnabled(true);
}
QStringList FolderModel::mimeTypes() const { return {mime}; }
QMimeData *FolderModel::mimeData(const QModelIndexList &indexes) const
{
    auto *data = new QMimeData;
    if (indexes.size() != 1 || indexes.first().parent().isValid()) return data;
    const auto id = indexes.first().data(Qt::UserRole).toString();
    if (!id.isEmpty()) data->setData(mime, id.toUtf8());
    return data;
}
bool FolderModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    if (action == Qt::IgnoreAction) return true;
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
}
