#pragma once

#include "core/account.h"
#include "storage/credentialstore.h"
#include <QProcess>
#include <QTimer>
#include <functional>

class QGuiApplication;

// Explicit live test only. Never uses the application's normal keychain service.
class GoogleCredentialRestartProbe final : public QObject
{
public:
    using Completion = std::function<void(bool)>;
    GoogleCredentialRestartProbe(const smoothmail::Account &account, QString registrationFile, Completion completion);
    void start(const QString &refreshToken);
    void cancel();
    bool started() const { return m_started; }
private:
    void launchChild();
    void cleanup(bool success);
    smoothmail::Account m_account;
    QString m_registrationFile;
    QString m_service;
    smoothmail::CredentialStore m_credentials;
    QProcess m_child;
    QTimer m_timeout;
    Completion m_completion;
    bool m_started = false, m_cleaning = false;
};

int runGoogleCredentialChild(QGuiApplication &application, const smoothmail::Account &account,
                             const QString &service, const QString &saslDirectory, const QString &clientSecret);
