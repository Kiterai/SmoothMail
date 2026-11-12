#include "ui/mainwindow.h"
#include "app/applicationcontroller.h"
#include "core/diagnostics.h"
#include <QApplication>
#include <QStandardPaths>
#include <QMessageBox>

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
    if (!smoothmail::diagnostics::start(dataPath + QStringLiteral("/diagnostics"))) {
        // Do not silently claim crash recording is enabled if the disk is unwritable.
        QMessageBox::critical(nullptr, QStringLiteral("診断ログを作成できません"), QStringLiteral("保存先のアクセス権と空き容量を確認してください。\n") + dataPath);
        return 2;
    }
    int result;
    {
    smoothmail::MainWindow window;
    smoothmail::ApplicationController controller(&window, dataPath + QStringLiteral("/smoothmail.sqlite"),
        qEnvironmentVariable("SMOOTHMAIL_SASL_PATH", QCoreApplication::applicationDirPath() + QStringLiteral("/sasl2")));
    window.show();
    result = application.exec();
    } // Worker shutdown is covered by the abnormal-session marker too.
    smoothmail::diagnostics::finish();
    return result;
}
