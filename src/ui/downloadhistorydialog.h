#pragma once
#include "core/downloadentry.h"
#include <QDialog>
class QTreeView;
class QStandardItemModel;
namespace smoothmail {
class DownloadHistoryDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit DownloadHistoryDialog(QWidget *parent = nullptr);
    void setEntries(const QList<DownloadEntry> &entries);
signals:
    void sourceRequested(const smoothmail::DownloadEntry &entry);
private:
    QTreeView *m_view;
    QStandardItemModel *m_model;
    QList<DownloadEntry> m_entries;
};
}
