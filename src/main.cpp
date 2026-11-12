#include "ui/mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("SmoothMail"));
    QCoreApplication::setApplicationName(QStringLiteral("SmoothMail"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    smoothmail::MainWindow window;
    window.show();
    return application.exec();
}
