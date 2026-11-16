#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSslSocket>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    database.setDatabaseName(QStringLiteral(":memory:"));
    // No network connection, persistent data or OS credential access.
    return database.open() && QSslSocket::supportsSsl() ? 0 : 1;
}
