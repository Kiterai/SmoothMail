#include <KCrash>
#include <KService>
#include <KLocalizedString>
#include <KProtocolInfo>
#include <KIO/DesktopExecParser>
#include <KIO/StoredTransferJob>
#include <KIO/Global>
#include <KSslErrorUiData>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <windows.h>

namespace {
class Environment final {
public:
    Environment(const char *key, const QByteArray &value) : m_key(key), m_value(qgetenv(key)), m_present(qEnvironmentVariableIsSet(key))
    { qputenv(key, value); }
    ~Environment() { if(m_present) qputenv(m_key.constData(), m_value); else qunsetenv(m_key.constData()); }
private:
    QByteArray m_key, m_value;
    bool m_present;
};
class OwnedHelper final {
public:
    explicit OwnedHelper(const QString &name) : path(QCoreApplication::applicationDirPath() + "/" + name)
    { created = QFile::copy(QStringLiteral(FIXTURE_EXE), path); }
    ~OwnedHelper() { if(created) QFile::remove(path); }
    QString path;
    bool created = false;
};
QByteArray moduleData(const wchar_t *name)
{
    const auto module = GetModuleHandleW(name);
    wchar_t path[32768] = {};
    const auto length = GetModuleFileNameW(module, path, 32768);
    if(!module || !length || length >= 32768) return {};
    QFile file(QString::fromWCharArray(path, int(length)));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}

class PortableKdeTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        KLocalizedString::setApplicationDomain("smoothmail-portable-kde-tests");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
        // Do not run behavior probes against an accidental old DLL.
        const auto crash = moduleData(L"KF6Crash.dll");
        const auto kio = moduleData(L"KF6KIOCore.dll");
        QVERIFY(!crash.isEmpty() && !kio.isEmpty());
        QCOMPARE(QCryptographicHash::hash(crash, QCryptographicHash::Sha256).toHex(), QByteArray(EXPECTED_Crash_SHA256));
        QCOMPARE(QCryptographicHash::hash(kio, QCryptographicHash::Sha256).toHex(), QByteArray(EXPECTED_KIOCore_SHA256));
        QVERIFY(kio.contains("The admin protocol is disabled in this build."));
    }
    void missingProgramFails()
    {
        KService service(QStringLiteral("fixture"), QStringLiteral("smoothmail-nonexistent-program-83f125"), QString());
        KIO::DesktopExecParser parser(service, {});
        QVERIFY(parser.resultingArguments().isEmpty());
        QVERIFY(!parser.errorMessage().isEmpty());
    }
    void normalBundledExecutableRemainsUsable()
    {
        OwnedHelper helper(QStringLiteral("smoothmail-portable-fixture.exe"));
        QVERIFY(helper.created);
        Environment path("PATH", {});
        KService service(QStringLiteral("fixture"), QStringLiteral("smoothmail-portable-fixture.exe"), QString());
        KIO::DesktopExecParser parser(service, {});
        const auto arguments = parser.resultingArguments();
        QVERIFY(!arguments.isEmpty());
        QCOMPARE(QFileInfo(arguments.first()).canonicalFilePath(), QFileInfo(helper.path).canonicalFilePath());
    }
    void missingKioexecDoesNotUseCurrentDirectory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(!QFileInfo::exists(QCoreApplication::applicationDirPath() + "/kioexec.exe"));
        QVERIFY(QFile::copy(QStringLiteral(FIXTURE_EXE), directory.filePath("kioexec.exe")));
        const auto current = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(current); });
        QVERIFY(QDir::setCurrent(directory.path()));
        Environment path("PATH", directory.path().toUtf8());
        KService service(QStringLiteral("fixture"), '"' + QCoreApplication::applicationFilePath() + '"', QString());
        KIO::DesktopExecParser parser(service, {QUrl::fromLocalFile(directory.filePath("dummy.txt"))});
        parser.setUrlsAreTempFiles(true);
        QVERIFY(parser.resultingArguments().isEmpty());
        QVERIFY(parser.errorMessage().contains(QStringLiteral("kioexec")));
    }
    void bundledKioexecIsResolvedWithoutExecution()
    {
        OwnedHelper helper(QStringLiteral("kioexec.exe"));
        QVERIFY(helper.created);
        QTemporaryDir directory;
        KService service(QStringLiteral("fixture"), '"' + QCoreApplication::applicationFilePath() + '"', QString());
        KIO::DesktopExecParser parser(service, {QUrl::fromLocalFile(directory.filePath("dummy.txt"))});
        parser.setUrlsAreTempFiles(true);
        const auto arguments = parser.resultingArguments();
        QVERIFY(!arguments.isEmpty());
        QCOMPARE(QFileInfo(arguments.first()).canonicalFilePath(), QFileInfo(helper.path).canonicalFilePath());
        QVERIFY(arguments.contains(QStringLiteral("--tempfiles")));
    }
    void crashHelperUsesRuntimeLocation()
    {
        // Finding a harmless fixture is tested; no crash or process launch occurs.
        Environment paths("LIBEXEC_PATH", {});
        Environment metadata("KCRASH_NO_METADATA", "1");
        const auto oldHandler = KCrash::crashHandler();
        const auto restore = qScopeGuard([&] {
            KCrash::setDrKonqiEnabled(false);
            KCrash::setCrashHandler(oldHandler);
        });
        OwnedHelper helper(QStringLiteral("drkonqi.exe"));
        QVERIFY(helper.created);
        KCrash::setDrKonqiEnabled(true);
        QVERIFY(KCrash::isDrKonqiEnabled());
    }
    void adminWorkerFailsBeforePluginConstruction()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto marker = directory.filePath("constructed");
        Environment markerEnvironment("SMOOTHMAIL_ADMIN_FIXTURE_MARKER", marker.toUtf8());
        const auto oldPaths = QCoreApplication::libraryPaths();
        const auto restore = qScopeGuard([&] { QCoreApplication::setLibraryPaths(oldPaths); });
        QCoreApplication::setLibraryPaths({QStringLiteral(FIXTURE_PLUGINS)});
        QVERIFY(KProtocolInfo::protocols().contains(QStringLiteral("admin")));
        auto *job = KIO::storedGet(QUrl(QStringLiteral("admin:/synthetic-never-read")), KIO::NoReload, KIO::HideProgressInfo);
        job->setAutoDelete(false);
        const auto cleanup = qScopeGuard([&] { delete job; });
        QSignalSpy finished(job, &KJob::result);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(job->error(), int(KIO::ERR_CANNOT_CREATE_WORKER));
        QVERIFY(job->errorText().contains(QStringLiteral("admin protocol is disabled")));
        QVERIFY(!QFileInfo::exists(marker));
    }
    void retainedCoreApis()
    {
        QVERIFY(KIO::favIconForUrl(QUrl(QStringLiteral("https://example.invalid"))).isEmpty());
        QSslSocket socket;
        const KSslErrorUiData data(&socket);
        KSslErrorUiData copy(data);
        Q_UNUSED(copy)
    }
    void builtInDataProtocolRemainsUsable()
    {
        auto *job = KIO::storedGet(QUrl(QStringLiteral("data:text/plain,portable-fixture")), KIO::NoReload, KIO::HideProgressInfo);
        job->setAutoDelete(false);
        const auto cleanup = qScopeGuard([&] { delete job; });
        QSignalSpy finished(job, &KJob::result);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(job->error(), 0);
        QCOMPARE(job->data(), QByteArray("portable-fixture"));
    }
};
QTEST_GUILESS_MAIN(PortableKdeTest)
#include "tst_portablekde.moc"
