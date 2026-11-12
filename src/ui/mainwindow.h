#pragma once

#include <QMainWindow>
#include "core/account.h"

class QCloseEvent;
class QSystemTrayIcon;
class QStandardItemModel;

namespace smoothmail {

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    // Tests and environments without a tray can opt out of desktop integration.
    explicit MainWindow(bool enableSystemTray = true, QWidget *parent = nullptr);
    void enableAccountManagement(bool enabled);
    void setAccounts(const QList<Account> &accounts);
signals:
    void addAccountRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createPanes();
    void createTray();
    void restoreWindow();

    QSystemTrayIcon *m_tray = nullptr;
    QStandardItemModel *m_folders = nullptr;
    bool m_exiting = false;
};

} // namespace smoothmail
