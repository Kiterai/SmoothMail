#include "google_credential_probe.h"
#include "mail/authenticationbroker.h"
#include "mail/mailservice.h"
#include "mail/oauthclient.h"
#include "storage/accountregistrar.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <iostream>

using namespace smoothmail;
namespace {
QString key(const QString &id, const QString &protocol)
{
    return AccountRegistrar::secretKey(id, protocol, AuthenticationMode::OAuth2);
}
}

GoogleCredentialRestartProbe::GoogleCredentialRestartProbe(const Account &account, QString file, Completion completion)
    : m_account(account), m_registrationFile(QFileInfo(file).absoluteFilePath()),
      m_service(QStringLiteral("SmoothMail-live-google-") + account.id), m_credentials(nullptr, m_service),
      m_completion(std::move(completion))
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { cancel(); });
    connect(&m_child, &QProcess::readyReadStandardOutput, this, [this] {
        // The same executable's child mode prints only fixed stage statuses.
        const auto output = m_child.readAllStandardOutput();
        std::cout << output.constData() << std::flush;
    });
    connect(&m_child, &QProcess::readyReadStandardError, this, [this] { m_child.readAllStandardError(); });
    connect(&m_child, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) cleanup(false);
    });
    connect(&m_child, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const auto output = m_child.readAllStandardOutput();
        std::cout << output.constData() << std::flush;
        m_child.readAllStandardError();
        cleanup(status == QProcess::NormalExit && code == 0);
    });
}

void GoogleCredentialRestartProbe::start(const QString &token)
{
    if (m_started) return;
    m_started = true;
    const auto grant = refreshGrant(token);
    if (grant.isEmpty() || !QFileInfo::exists(m_registrationFile)) { cleanup(false); return; }
    std::cout << "KEYCHAIN_TEMPORARY_WRITE_START" << std::endl;
    m_credentials.write(key(m_account.id, QStringLiteral("imap")), grant, [this, grant](const QString &, const QString &error) {
        if (m_cleaning) return;
        if (!error.isEmpty()) { cleanup(false); return; }
        m_credentials.write(key(m_account.id, QStringLiteral("smtp")), grant, [this](const QString &, const QString &error) {
            if (m_cleaning) return;
            if (!error.isEmpty()) { cleanup(false); return; }
            std::cout << "KEYCHAIN_TEMPORARY_WRITE_OK" << std::endl;
            launchChild();
        });
    });
}

void GoogleCredentialRestartProbe::launchChild()
{
    std::cout << "CREDENTIAL_RESTART_CHILD_START" << std::endl;
    // Only public identifiers/file paths are in argv. Tokens are transferred
    // exclusively through the OS credential store, never argv/stdin/files.
    m_child.start(QCoreApplication::applicationFilePath(), {
        QStringLiteral("--credential-child"), QStringLiteral("--account-id"), m_account.id,
        QStringLiteral("--keychain-service"), m_service, QStringLiteral("--client-id"), m_account.imap.oauthClientId,
        QStringLiteral("--email"), m_account.email, QStringLiteral("--registration-file"), m_registrationFile});
    m_timeout.start(120000);
}

void GoogleCredentialRestartProbe::cancel()
{
    if (m_cleaning) return;
    if (m_child.state() != QProcess::NotRunning) {
        m_child.kill(); // The QProcess::finished handler performs cleanup.
    } else {
        cleanup(false);
    }
}

void GoogleCredentialRestartProbe::cleanup(bool success)
{
    if (m_cleaning) return;
    m_cleaning = true;
    m_timeout.stop();
    m_credentials.remove(key(m_account.id, QStringLiteral("imap")), [this, success](const QString &, const QString &imapError) {
        m_credentials.remove(key(m_account.id, QStringLiteral("smtp")), [this, success, imapError](const QString &, const QString &smtpError) {
            const bool removed = imapError.isEmpty() && smtpError.isEmpty();
            std::cout << "KEYCHAIN_TEST_CLEANUP=" << (removed ? "OK" : "FAILED") << std::endl;
            if (!removed) std::cout << "KEYCHAIN_TEST_NAMESPACE=" << m_service.toLatin1().constData() << std::endl;
            std::cout << "CREDENTIAL_RESTART_TEST=" << (success && removed ? "OK" : "FAILED") << std::endl;
            m_completion(success && removed);
        });
    });
}

int runGoogleCredentialChild(QGuiApplication &app, const Account &account, const QString &service,
                             const QString &plugins, const QString &secret)
{
    if (!validateAccount(account).isEmpty() || service != QStringLiteral("SmoothMail-live-google-") + account.id) {
        std::cout << "CREDENTIAL_CHILD_SETUP_INVALID" << std::endl;
        return 2;
    }
    // AuthenticationBroker reads the Google Desktop secret from its process
    // environment. This child loads it locally; it is never placed in argv.
    qputenv("SMOOTHMAIL_GOOGLE_CLIENT_SECRET", secret.toUtf8());
    CredentialStore credentials(nullptr, service);
    AuthenticationBroker broker(&credentials);
    MailService mail(plugins);
    QString imapToken;
    bool completed = false;
    const auto finish = [&](int code) { completed = true; app.exit(code); };
    QObject::connect(&mail, &MailService::finished, &app, [&](const QString &smtpError, const QString &imapError) {
        std::cout << "CHILD_SMTP_XOAUTH2=" << (smtpError.isEmpty() ? "OK" : "FAILED")
                  << "\nCHILD_IMAP_XOAUTH2=" << (imapError.isEmpty() ? "OK" : "FAILED") << std::endl;
        finish(smtpError.isEmpty() && imapError.isEmpty() ? 0 : 1);
    });
    QTimer::singleShot(110000, &app, [&] { std::cout << "CREDENTIAL_CHILD_TIMEOUT" << std::endl; finish(1); });
    QTimer::singleShot(0, &app, [&] {
        broker.acquire(account, QStringLiteral("imap"), [&](const QString &token, const QString &error) {
            if (!error.isEmpty()) { std::cout << "CHILD_IMAP_RESTORE_REFRESH=FAILED" << std::endl; finish(1); return; }
            imapToken = token;
            std::cout << "CHILD_IMAP_RESTORE_REFRESH=OK" << std::endl;
            broker.acquire(account, QStringLiteral("smtp"), [&](const QString &smtpToken, const QString &smtpError) {
                if (!smtpError.isEmpty()) { std::cout << "CHILD_SMTP_RESTORE_REFRESH=FAILED" << std::endl; finish(1); return; }
                std::cout << "CHILD_SMTP_RESTORE_REFRESH=OK" << std::endl;
                mail.test(account, smtpToken, imapToken);
            });
        });
    });
    const int result = app.exec();
    return completed ? result : 1;
}
