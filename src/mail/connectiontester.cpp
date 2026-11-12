#include "connectiontester.h"

#include <KIMAP/CapabilitiesJob>
#include <KIMAP/LoginJob>
#include <KIMAP/Session>
#include <KSMTP/LoginJob>
#include <KSMTP/Session>
#include <KLocalizedString>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMutex>
#include <QTimer>
#include <QSslSocket>
extern "C" {
#include <sasl/sasl.h>
}

namespace smoothmail {
namespace {
QString initializeSasl(const QString &directory)
{
    static QMutex mutex;
    static QString configured;
    const QMutexLocker lock(&mutex);
    const auto path = QDir(directory).absolutePath();
    if (!QDir(path).exists()) return QStringLiteral("SASLプラグインの配置先がありません。");
    if (!configured.isEmpty()) {
        return configured == path ? QString{} : QStringLiteral("実行中にSASL配置先を変更することはできません。");
    }
    auto encoded = QDir::toNativeSeparators(path).toUtf8();
    if (sasl_set_path(SASL_PATH_TYPE_PLUGIN, encoded.data()) != SASL_OK) {
        return QStringLiteral("SASLプラグインを初期化できません。");
    }
    configured = path;
    return {};
}

QString runJob(KJob &job)
{
    job.setAutoDelete(false);
    QEventLoop loop;
    QTimer timer;
    QElapsedTimer elapsed;
    bool completed = false;
    QObject::connect(&job, &KJob::result, &loop, [&] { completed = true; loop.quit(); });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (elapsed.elapsed() >= 30000 || QThread::currentThread()->isInterruptionRequested()) loop.quit();
    });
    elapsed.start();
    timer.start(100);
    job.start();
    if (!completed) loop.exec();
    if (!completed) return QStringLiteral("接続処理がタイムアウトまたは中断されました。");
    if (job.error()) return job.errorText().isEmpty() ? QStringLiteral("認証に失敗しました。") : job.errorText().left(1024);
    return {};
}

template<typename Base> class RejectCertificateErrors final : public Base
{
public:
    bool rejected = false;
    bool ignoreSslError(const KSslErrorUiData &) override { rejected = true; return false; }
};

QString testSmtp(const ServerSettings &settings, const QString &secret)
{
    KSmtp::Session session(settings.host, settings.port);
    auto proxy = QSharedPointer<RejectCertificateErrors<KSmtp::SessionUiProxy>>::create();
    session.setUiProxy(proxy);
    session.setSocketTimeout(15000);
    switch (settings.encryption) {
    case EncryptionMode::None: session.setEncryptionMode(KSmtp::Session::Unencrypted); break;
    case EncryptionMode::ImplicitTls: session.setEncryptionMode(KSmtp::Session::TLS); break;
    case EncryptionMode::StartTls: session.setEncryptionMode(KSmtp::Session::STARTTLS); break;
    }
    QEventLoop loop;
    QTimer timer;
    QElapsedTimer elapsed;
    QString connectionError;
    QObject::connect(&session, &KSmtp::Session::stateChanged, &loop, [&](KSmtp::Session::State state) {
        if (state == KSmtp::Session::NotAuthenticated || state == KSmtp::Session::Disconnected) loop.quit();
    });
    QObject::connect(&session, &KSmtp::Session::connectionError, &loop, [&](const QString &error) {
        connectionError = error.left(1024);
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (elapsed.elapsed() >= 30000 || QThread::currentThread()->isInterruptionRequested()) loop.quit();
    });
    timer.start(100);
    elapsed.start();
    session.open();
    loop.exec();
    if (proxy->rejected) return QStringLiteral("TLS証明書の検証に失敗しました。");
    if (session.state() != KSmtp::Session::NotAuthenticated) {
        return connectionError.isEmpty() ? QStringLiteral("指定された暗号化方式で接続できませんでした。") : connectionError;
    }
    // KSMTP reaches NotAuthenticated only after its requested TLS handshake;
    // the proxy above never permits certificate errors. Do not infer this from
    // TCP connection state, nor reuse it after a disconnection.
    const bool encrypted = settings.encryption != EncryptionMode::None;
    const auto decision = authenticationDecision(settings.encryption, settings.authentication,
                                                  {encrypted, encrypted}, session.availableAuthModes());
    if (!decision.allowed()) return decision.error;
    KSmtp::LoginJob login(&session);
    switch (settings.authentication) {
    case AuthenticationMode::Plain: login.setPreferedAuthMode(KSmtp::LoginJob::Plain); break;
    case AuthenticationMode::Login: login.setPreferedAuthMode(KSmtp::LoginJob::Login); break;
    case AuthenticationMode::OAuth2: login.setPreferedAuthMode(KSmtp::LoginJob::XOAuth2); break;
    }
    login.setUserName(settings.user);
    login.setPassword(secret); // Only after exact mechanism and transport checks.
    const auto error = runJob(login);
    if (!error.isEmpty()) return error;
    if (session.state() != KSmtp::Session::Authenticated) return QStringLiteral("SMTP認証済み状態になりませんでした。");
    session.quit();
    return {};
}

QString testImap(const ServerSettings &settings, const QString &secret)
{
    KIMAP::Session session(settings.host, settings.port);
    auto proxy = QSharedPointer<RejectCertificateErrors<KIMAP::SessionUiProxy>>::create();
    session.setUiProxy(proxy);
    session.setTimeout(15);
    // Plaintext is permitted only when explicitly selected. TLS paths perform
    // post-TLS CAPABILITY and exact-mechanism checks inside KIMAP LoginJob.
    // Unlike KSMTP, KIMAP cannot split its TLS handshake into a separate job.
    if (settings.encryption == EncryptionMode::None) {
        KIMAP::CapabilitiesJob capabilities(&session);
        const auto error = runJob(capabilities);
        if (!error.isEmpty()) return error;
        QStringList mechanisms;
        for (const auto &value : capabilities.capabilities()) {
            if (value.startsWith(QStringLiteral("AUTH="))) mechanisms.append(value.mid(5));
        }
        const auto decision = authenticationDecision(EncryptionMode::None, settings.authentication, {}, mechanisms);
        if (!decision.allowed()) return decision.error;
    }
    KIMAP::LoginJob login(&session);
    switch (settings.encryption) {
    case EncryptionMode::None: login.setEncryptionMode(KIMAP::LoginJob::Unencrypted); break;
    case EncryptionMode::ImplicitTls: login.setEncryptionMode(KIMAP::LoginJob::SSLorTLS); break;
    case EncryptionMode::StartTls: login.setEncryptionMode(KIMAP::LoginJob::STARTTLS); break;
    }
    switch (settings.authentication) {
    case AuthenticationMode::Plain: login.setAuthenticationMode(KIMAP::LoginJob::Plain); break;
    case AuthenticationMode::Login: login.setAuthenticationMode(KIMAP::LoginJob::Login); break;
    case AuthenticationMode::OAuth2: login.setAuthenticationMode(KIMAP::LoginJob::XOAuth2); break;
    }
    login.setUserName(settings.user);
    login.setPassword(secret); // KIMAP must not transmit until its internal TLS/mechanism gate passes.
    const auto error = runJob(login);
    if (proxy->rejected) return QStringLiteral("TLS証明書の検証に失敗しました。");
    if (!error.isEmpty()) return error;
    if (settings.encryption != EncryptionMode::None && login.encryptionMode() == KIMAP::LoginJob::Unencrypted) {
        return QStringLiteral("指定したTLS接続が確立されていません。");
    }
    if (session.state() != KIMAP::Session::Authenticated) return QStringLiteral("IMAP4認証済み状態になりませんでした。");
    session.close();
    return {};
}
}

ConnectionTester::ConnectionTester(QString plugins, QObject *parent)
    : QObject(parent), m_worker(new QObject), m_pluginDirectory(std::move(plugins))
{
    // KI18n installs an application event filter on first use. Initialize it
    // on the owning (GUI) thread, not from a mail worker's error path.
    KLocalizedString::setApplicationDomain("smoothmail");
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &ConnectionTester::finished, this, [this] { m_busy = false; });
    m_thread.start();
}

ConnectionTester::~ConnectionTester()
{
    m_thread.requestInterruption();
    m_thread.quit();
    m_thread.wait();
}

void ConnectionTester::test(const Account &account, const QString &smtpSecret, const QString &imapSecret)
{
    if (m_busy) return;
    m_busy = true;
    QMetaObject::invokeMethod(m_worker, [this, account, smtpSecret, imapSecret] {
        auto error = validateAccount(account);
        if (error.isEmpty()) error = initializeSasl(m_pluginDirectory);
        if (!error.isEmpty()) { emit finished(error, error); return; }
        const auto smtpError = testSmtp(account.smtp, smtpSecret);
        const auto imapError = m_thread.isInterruptionRequested() ? QStringLiteral("中断しました。")
                                                               : testImap(account.imap, imapSecret);
        emit finished(smtpError, imapError);
    }, Qt::QueuedConnection);
}
} // namespace smoothmail
