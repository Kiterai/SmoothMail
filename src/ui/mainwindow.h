#pragma once

#include <QMainWindow>

class QCloseEvent;
class QSystemTrayIcon;

namespace smoothmail {

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    // Tests and environments without a tray can opt out of desktop integration.
    explicit MainWindow(bool enableSystemTray = true, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createPanes();
    void createTray();
    void restoreWindow();

    QSystemTrayIcon *m_tray = nullptr;
    bool m_exiting = false;
};

} // namespace smoothmail
