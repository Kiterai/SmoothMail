#include "downloadhistorydialog.h"
#include <QTreeView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QMenu>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QScreen>

namespace smoothmail {
DownloadHistoryDialog::DownloadHistoryDialog(QWidget *parent) : QDialog(parent), m_view(new QTreeView(this)), m_model(new QStandardItemModel(this))
{
    setWindowTitle(QStringLiteral("ダウンロード履歴"));
    const auto available = screen()->availableGeometry().size();
    resize(qMin(900, available.width() - 80), qMin(420, available.height() - 80));
    auto *layout = new QVBoxLayout(this); layout->addWidget(m_view);
    m_model->setHorizontalHeaderLabels({QStringLiteral("元メッセージ"), QStringLiteral("保存日"), QStringLiteral("保存パス"), QStringLiteral("サイズ"), QStringLiteral("MIMEタイプ")});
    m_view->setModel(m_model); m_view->setRootIsDecorated(false);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTreeView::customContextMenuRequested, this, [this](const QPoint &point) {
        const auto index = m_view->indexAt(point);
        if (!index.isValid()) return;
        const int row = index.siblingAtColumn(0).data(Qt::UserRole).toInt();
        if (row < 0 || row >= m_entries.size()) return;
        const auto entry = m_entries.at(row);
        QMenu menu(this);
        auto *source = menu.addAction(QStringLiteral("元メッセージ..."));
        auto *directory = menu.addAction(QStringLiteral("ファイルの保存先..."));
        const auto *chosen = menu.exec(m_view->viewport()->mapToGlobal(point));
        if (chosen == source) emit sourceRequested(entry);
        if (chosen == directory) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(entry.path).absolutePath()));
    });
}

void DownloadHistoryDialog::setEntries(const QList<DownloadEntry> &entries)
{
    m_entries = entries; m_model->removeRows(0, m_model->rowCount());
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const auto &entry = entries.at(i);
        double size = double(entry.size); int unit = 0;
        const QStringList units{QStringLiteral("B"), QStringLiteral("kB"), QStringLiteral("MB"), QStringLiteral("GB"), QStringLiteral("TB")};
        while (size >= 1000 && unit < units.size() - 1) { size /= 1000; ++unit; }
        const QStringList cells{entry.subject, entry.savedAt.toLocalTime().toString(QStringLiteral("yyyy/MM/dd HH:mm")), entry.path,
            QString::number(size, 'f', unit ? 2 : 0) + u' ' + units.at(unit), entry.mimeType};
        QList<QStandardItem *> row;
        for (const auto &cell : cells) { auto *item = new QStandardItem(cell); item->setEditable(false); row.append(item); }
        row.first()->setData(i, Qt::UserRole); m_model->appendRow(row);
    }
    for (int i = 0; i < 5; ++i) m_view->resizeColumnToContents(i);
}
}
