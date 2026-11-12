#pragma once
#include <QStandardItemModel>

namespace smoothmail {
class FolderModel final : public QStandardItemModel
{
    Q_OBJECT
public:
    explicit FolderModel(QObject *parent = nullptr);
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;
signals:
    void accountOrderChanged(const QStringList &accountIds);
};
}
