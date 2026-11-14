#include <QObject>
#include <QFile>

class AdminFixture final : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker" FILE "adminfixture.json")
public:
    AdminFixture()
    {
        // Only a test-owned marker; no worker implementation or external action.
        QFile file(qEnvironmentVariable("SMOOTHMAIL_ADMIN_FIXTURE_MARKER"));
        if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            file.write("unexpected plugin construction");
    }
};
#include "adminfixture.moc"
