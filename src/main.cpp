#include "ui/mainwindow.h"
#include "ui/accountdialog.h"
#include "storage/accountregistrar.h"

#include <QApplication>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QStatusBar>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("SmoothMail"));
    QCoreApplication::setApplicationName(QStringLiteral("SmoothMail"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataPath.isEmpty()) return 1;
    smoothmail::AccountRepository repository(dataPath + QStringLiteral("/smoothmail.sqlite"));
    smoothmail::CredentialStore credentials;
    smoothmail::AccountRegistrar registrar(&repository, &credentials);
    smoothmail::MainWindow window;
    window.enableAccountManagement(true);
    QObject::connect(&repository, &smoothmail::AccountRepository::loaded,
                     &window, &smoothmail::MainWindow::setAccounts);
    QObject::connect(&repository, &smoothmail::AccountRepository::failed, &window,
        [&window](const QString &operation, const QString &error) {
            if (operation == QStringLiteral("load")) {
                window.enableAccountManagement(false);
                window.statusBar()->showMessage(QStringLiteral("アカウント設定を読み込めません: ") + error);
            }
        });
    QPointer<smoothmail::AccountDialog> dialog;
    QObject::connect(&window, &smoothmail::MainWindow::addAccountRequested, &window, [&] {
        if (dialog) {
            dialog->raise();
            dialog->activateWindow();
            return;
        }
        dialog = new smoothmail::AccountDialog(&window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(dialog, &smoothmail::AccountDialog::saveRequested,
                         &registrar, &smoothmail::AccountRegistrar::add);
        dialog->open();
    });
    QObject::connect(&registrar, &smoothmail::AccountRegistrar::finished, &window,
        [&](const QString &, const QString &error) { if (dialog) dialog->saveFinished(error); });
    window.show();
    return application.exec();
}
