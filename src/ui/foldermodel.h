#pragma once
#include <QStandardItemModel>
#include "core/mailrecord.h"

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
    void messageMoveBatchStarted();
    void messageMoveBatchFinished();
    void accountOrderChanged(const QStringList &accountIds);
    void folderMoveRequested(const QString &id, const QString &source, const QString &parent, QChar separator);
    void messageMoveRequested(const smoothmail::MailRecord &identity, const QString &destination);
};
class MessageListModel final : public QStandardItemModel {
    Q_OBJECT
public:
    explicit MessageListModel(QObject *parent = nullptr) : QStandardItemModel(parent) {}
    QList<MailRecord> records;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }
};
}
