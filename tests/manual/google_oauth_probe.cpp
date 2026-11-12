#include "core/account.h"
#include "mail/mailservice.h"
#include "mail/oauthclient.h"

#include <QCommandLineParser>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QTimer>
#include <QUuid>
#include <iostream>

// Opt-in live probe: uses the actual production OAuthClient and MailService.
// No password, token file, mail content, persistent account, send or append.
int main(int argc, char **argv)
{
    // Protocol debug output/session logs must never expose live credentials.
    qunsetenv("KIMAP_LOGFILE");
    qunsetenv("KSMTP_SESSION_LOG");
    qunsetenv("SSLKEYLOGFILE");
    qunsetenv("QT_SSLKEYLOGFILE");
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SmoothMail Google OAuth probe"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Live Google OAuth + refresh + SMTP/IMAP authentication only. No mail is read or sent."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("client-id"), QStringLiteral("Google Desktop OAuth client ID (not a password)."), QStringLiteral("id")});
    parser.addOption({QStringLiteral("email"), QStringLiteral("Test account to suggest in the consent browser."), QStringLiteral("address")});
    parser.process(app);
    const auto clientId = parser.value(QStringLiteral("client-id"));
    const auto email = parser.value(QStringLiteral("email"));
    if (!clientId.endsWith(QStringLiteral(".apps.googleusercontent.com")) || !email.endsWith(QStringLiteral("@gmail.com"))) {
        std::cerr << "SETUP_REQUIRED: specify --client-id and --email for the Google test account.\n";
        return 2;
    }
    const auto secret = qEnvironmentVariable("SMOOTHMAIL_GOOGLE_CLIENT_SECRET");
    smoothmail::OAuthClient authorization, refresh;
    smoothmail::MailService mail(qEnvironmentVariable("SMOOTHMAIL_SASL_PATH",
        QCoreApplication::applicationDirPath() + QStringLiteral("/sasl2")));
    const auto fail = [&app](const QString &error) {
        // OAuthClient emits only application-generated errors, no raw response.
        std::cout << "OAUTH_FAILED: " << error.toUtf8().constData() << std::endl;
        app.exit(1);
    };
    QObject::connect(&authorization, &smoothmail::OAuthClient::failed, &app, fail);
    QObject::connect(&refresh, &smoothmail::OAuthClient::failed, &app, fail);
    QObject::connect(&authorization, &smoothmail::OAuthClient::authorizationUrlReady, &app, [&app](const QUrl &url) {
        std::cout << "BROWSER_CONSENT_REQUIRED: sign in with the test account and approve the Gmail permission in the opened browser.\n"
                     "Do not paste passwords, callback URLs or tokens into chat. Waiting up to five minutes." << std::endl;
        if (!QDesktopServices::openUrl(url)) {
            std::cout << "BROWSER_OPEN_FAILED" << std::endl;
            app.exit(1);
        }
    });
    QObject::connect(&authorization, &smoothmail::OAuthClient::tokensReady, &app,
        [&](const smoothmail::OAuthTokens &tokens) {
            std::cout << "AUTHORIZATION_CODE_EXCHANGE_OK\nREFRESH_CHECK_START" << std::endl;
            refresh.refresh(QStringLiteral("google"), clientId, tokens.refreshToken, secret);
        });
    QObject::connect(&refresh, &smoothmail::OAuthClient::tokensReady, &app,
        [&](const smoothmail::OAuthTokens &tokens) {
            std::cout << "REFRESH_OK\nMAIL_AUTH_CHECK_START" << std::endl;
            smoothmail::Account account{QUuid::createUuid().toString(QUuid::WithoutBraces), email,
                {QStringLiteral("smtp.gmail.com"), 465, email, smoothmail::AuthenticationMode::OAuth2,
                 smoothmail::EncryptionMode::ImplicitTls, QStringLiteral("google"), clientId},
                {QStringLiteral("imap.gmail.com"), 993, email, smoothmail::AuthenticationMode::OAuth2,
                 smoothmail::EncryptionMode::ImplicitTls, QStringLiteral("google"), clientId}};
            mail.test(account, tokens.accessToken, tokens.accessToken);
        });
    QObject::connect(&mail, &smoothmail::MailService::finished, &app,
        [&app](const QString &smtpError, const QString &imapError) {
            // Do not print raw mail server response/error strings.
            std::cout << "SMTP_XOAUTH2=" << (smtpError.isEmpty() ? "OK" : "FAILED")
                      << "\nIMAP_XOAUTH2=" << (imapError.isEmpty() ? "OK" : "FAILED") << std::endl;
            app.exit(smtpError.isEmpty() && imapError.isEmpty() ? 0 : 1);
        });
    QTimer::singleShot(7 * 60 * 1000, &app, [&app] {
        std::cout << "PROBE_TIMEOUT" << std::endl;
        app.exit(1);
    });
    QTimer::singleShot(0, &app, [&] {
        std::cout << "Starting live OAuth probe. No mail will be read or sent." << std::endl;
        authorization.authorize(QStringLiteral("google"), clientId, email, secret);
    });
    return app.exec();
}
