#include "ui/mainwindow.h"
#include "app/applicationcontroller.h"
#include <QApplication>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    // Mail KJobs must not decide application lifetime while the main window
    // is hidden in the tray. Normal window close/tray Exit still govern quit.
    QCoreApplication::setQuitLockEnabled(false);
    QCoreApplication::setOrganizationName(QStringLiteral("SmoothMail"));
    QCoreApplication::setApplicationName(QStringLiteral("SmoothMail"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    const auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataPath.isEmpty()) return 1;
    smoothmail::MainWindow window;
    smoothmail::ApplicationController controller(&window, dataPath + QStringLiteral("/smoothmail.sqlite"),
        qEnvironmentVariable("SMOOTHMAIL_SASL_PATH", QCoreApplication::applicationDirPath() + QStringLiteral("/sasl2")));
    window.show();
    return application.exec();
}
