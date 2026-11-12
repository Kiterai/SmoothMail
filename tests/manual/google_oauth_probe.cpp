#include "core/account.h"
#include "mail/mailservice.h"
#include "mail/oauthclient.h"

#include <QCommandLineParser>
#include <QDesktopServices>
#include <QEventLoopLocker>
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>
#include <iostream>

// Opt-in live probe: uses the actual production OAuthClient and MailService.
// Default: authentication only. --self-mail-test explicitly enables one
// self-addressed send, one draft, and in-memory INBOX/draft synchronization.
// No passwords, token files, persistent account or logging of mail content.
int main(int argc, char **argv)
{
    // Protocol debug output/session logs must never expose live credentials.
    qunsetenv("KIMAP_LOGFILE");
    qunsetenv("KSMTP_SESSION_LOG");
    qunsetenv("SSLKEYLOGFILE");
    qunsetenv("QT_SSLKEYLOGFILE");
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    QGuiApplication app(argc, argv);
    // KJob owns a QEventLoopLocker. Releasing the last job can otherwise quit
    // this windowless QGuiApplication before the worker completion is delivered.
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setQuitLockEnabled(false);
    QCoreApplication::setApplicationName(QStringLiteral("SmoothMail Google OAuth probe"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Live Google OAuth + refresh + SMTP/IMAP probe. Mail operations require --self-mail-test."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("client-id"), QStringLiteral("Google Desktop OAuth client ID (not a password)."), QStringLiteral("id")});
    parser.addOption({QStringLiteral("email"), QStringLiteral("Test account to suggest in the consent browser."), QStringLiteral("address")});
    parser.addOption({QStringLiteral("registration-file"), QStringLiteral("Local downloaded Google Desktop client JSON; contents are never printed."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("self-test-lifecycle"), QStringLiteral("Offline lifecycle regression; no browser, credentials or network.")});
    parser.addOption({QStringLiteral("self-mail-test"), QStringLiteral("Send ONE UTF-8 test mail to this same account, append ONE draft, and synchronize INBOX/drafts to verify both. Does not delete mail or print contents.")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("self-test-lifecycle"))) {
        bool completed = false;
        QTimer::singleShot(0, &app, [&] {
            { QEventLoopLocker locker; }
            QTimer::singleShot(50, &app, [&] { completed = true; app.exit(0); });
        });
        const int result = app.exec();
        std::cout << (completed ? "PROBE_LIFECYCLE_OK" : "PROBE_INCOMPLETE") << std::endl;
        return completed ? result : 1;
    }
    const auto clientId = parser.value(QStringLiteral("client-id"));
    const auto email = parser.value(QStringLiteral("email"));
    if (!clientId.endsWith(QStringLiteral(".apps.googleusercontent.com")) || !email.endsWith(QStringLiteral("@gmail.com"))) {
        std::cerr << "SETUP_REQUIRED: specify --client-id and --email for the Google test account.\n";
        return 2;
    }
    auto secret = qEnvironmentVariable("SMOOTHMAIL_GOOGLE_CLIENT_SECRET");
    if (parser.isSet(QStringLiteral("registration-file"))) {
        QFile file(parser.value(QStringLiteral("registration-file")));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
            std::cerr << "SETUP_REQUIRED: cannot read the local registration JSON or it is too large.\n";
            return 2;
        }
        const auto installed = QJsonDocument::fromJson(file.readAll()).object().value("installed").toObject();
        if (installed.value("client_id").toString() != clientId || installed.value("client_secret").toString().isEmpty()) {
            std::cerr << "SETUP_REQUIRED: expected Desktop (installed) registration JSON with the matching client ID and a client secret.\n";
            return 2;
        }
        secret = installed.value("client_secret").toString();
    }
    smoothmail::OAuthClient authorization, refresh;
    smoothmail::MailService mail(qEnvironmentVariable("SMOOTHMAIL_SASL_PATH",
        QCoreApplication::applicationDirPath() + QStringLiteral("/sasl2")));
    const bool selfMailTest = parser.isSet(QStringLiteral("self-mail-test"));
    smoothmail::Account account;
    QString accessToken;
    smoothmail::ComposeData selfMessage, draftMessage;
    QString draftFolder;
    enum class Stage { Authentication, Send, AppendDraft, ReadInbox, ReadDraft };
    Stage stage = Stage::Authentication;
    int readAttempts = 0;
    bool completed = false;
    const auto finish = [&](int result) { completed = true; app.exit(result); };
    const auto fail = [&](const QString &error) {
        // OAuthClient emits only application-generated errors, no raw response.
        std::cout << "OAUTH_FAILED: " << error.toUtf8().constData() << std::endl;
        finish(1);
    };
    QObject::connect(&authorization, &smoothmail::OAuthClient::failed, &app, fail);
    QObject::connect(&refresh, &smoothmail::OAuthClient::failed, &app, fail);
    QObject::connect(&authorization, &smoothmail::OAuthClient::authorizationUrlReady, &app, [&](const QUrl &url) {
        std::cout << "BROWSER_CONSENT_REQUIRED: sign in with the test account and approve the Gmail permission in the opened browser.\n"
                     "Do not paste passwords, callback URLs or tokens into chat. Waiting up to five minutes." << std::endl;
        if (!QDesktopServices::openUrl(url)) {
            std::cout << "BROWSER_OPEN_FAILED" << std::endl;
            finish(1);
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
            account = {QUuid::createUuid().toString(QUuid::WithoutBraces), email,
                {QStringLiteral("smtp.gmail.com"), 465, email, smoothmail::AuthenticationMode::OAuth2,
                 smoothmail::EncryptionMode::ImplicitTls, QStringLiteral("google"), clientId},
                {QStringLiteral("imap.gmail.com"), 993, email, smoothmail::AuthenticationMode::OAuth2,
                 smoothmail::EncryptionMode::ImplicitTls, QStringLiteral("google"), clientId}};
            if (selfMailTest) accessToken = tokens.accessToken;
            mail.test(account, tokens.accessToken, tokens.accessToken);
        });
    QObject::connect(&mail, &smoothmail::MailService::finished, &app,
        [&](const QString &smtpError, const QString &imapError) {
            // Do not print raw mail server response/error strings.
            std::cout << "SMTP_XOAUTH2=" << (smtpError.isEmpty() ? "OK" : "FAILED")
                      << "\nIMAP_XOAUTH2=" << (imapError.isEmpty() ? "OK" : "FAILED") << std::endl;
            if (!smtpError.isEmpty() || !imapError.isEmpty()) { finish(1); return; }
            if (!selfMailTest) { finish(0); return; }
            const auto runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            std::cout << "SELF_MAIL_TEST_RUN_ID=" << runId.toLatin1().constData() << std::endl;
            selfMessage.from = email;
            selfMessage.to = email; // No user-configurable third-party recipients.
            selfMessage.subject = QStringLiteral("SmoothMail live test 日本語 ") + runId;
            selfMessage.body = QStringLiteral("SmoothMail開発用の自己宛テストです。\nUTF-8 subject/body roundtrip.\nRun: ") + runId;
            draftMessage = selfMessage;
            draftMessage.subject += QStringLiteral(" [draft]");
            stage = Stage::Send;
            mail.submit(account, accessToken, selfMessage, false); // Exactly one attempt; never automatically resend.
        });
    QObject::connect(&mail, &smoothmail::MailService::submissionFinished, &app,
        [&](const QString &, bool savedAsDraft, const QString &error) {
            if (!selfMailTest) { finish(1); return; }
            if (stage == Stage::Send && !savedAsDraft) {
                std::cout << "SMTP_SELF_SEND=" << (error.isEmpty() ? "OK" : "FAILED_NO_RETRY") << std::endl;
                if (!error.isEmpty()) { finish(1); return; }
                stage = Stage::AppendDraft;
                mail.submit(account, accessToken, draftMessage, true);
            } else if (stage == Stage::AppendDraft && savedAsDraft) {
                std::cout << "IMAP_DRAFT_APPEND=" << (error.isEmpty() ? "OK" : "FAILED_NO_RETRY") << std::endl;
                if (!error.isEmpty()) { finish(1); return; }
                stage = Stage::ReadInbox;
                readAttempts = 1;
                mail.synchronize(account, accessToken, QStringLiteral("INBOX"));
            } else {
                std::cout << "UNEXPECTED_SUBMISSION_RESULT" << std::endl;
                finish(1);
            }
        });
    QObject::connect(&mail, &smoothmail::MailService::synchronized, &app,
        [&](const QString &, const QString &folder, const QList<smoothmail::MailFolder> &folders,
            const QList<smoothmail::MailRecord> &messages, const QString &error) {
            const bool inbox = stage == Stage::ReadInbox;
            if (!selfMailTest || (!inbox && stage != Stage::ReadDraft)) { finish(1); return; }
            if (!error.isEmpty()) {
                std::cout << (inbox ? "INBOX_SYNC=FAILED" : "DRAFT_SYNC=FAILED") << std::endl;
                finish(1); return;
            }
            for (const auto &candidate : folders) {
                bool drafts = false, selectable = true;
                for (const auto &flag : candidate.flags) {
                    if (flag.toUpper() == "\\DRAFTS") drafts = true;
                    if (flag.toUpper() == "\\NOSELECT") selectable = false;
                }
                if (drafts && selectable) draftFolder = candidate.name;
            }
            const auto &expected = inbox ? selfMessage : draftMessage;
            bool found = false;
            for (const auto &record : messages) {
                if (record.document.subject != expected.subject) continue;
                bool bodyMatches = false;
                for (const auto &part : record.document.parts) {
                    if (part.mediaType == QStringLiteral("text/plain") && part.text.trimmed() == expected.body.trimmed()) bodyMatches = true;
                }
                found = bodyMatches && record.document.senderAddress.compare(email, Qt::CaseInsensitive) == 0
                    && record.document.recipients.contains(email, Qt::CaseInsensitive)
                    && smoothmail::validMessageId(record.document.messageId) && record.uid > 0 && record.uidValidity > 0;
                if (found) break;
            }
            if (!found && readAttempts < 6) {
                ++readAttempts;
                QTimer::singleShot(5000, &app, [&, folder] { mail.synchronize(account, accessToken, folder); });
                return;
            }
            std::cout << (inbox ? "INBOX_UTF8_READBACK=" : "DRAFT_UTF8_READBACK=") << (found ? "OK" : "NOT_FOUND_OR_MISMATCH") << std::endl;
            if (!found) { finish(1); return; }
            if (inbox) {
                if (draftFolder.isEmpty()) { std::cout << "DRAFT_FOLDER_NOT_FOUND" << std::endl; finish(1); return; }
                stage = Stage::ReadDraft;
                readAttempts = 1;
                mail.synchronize(account, accessToken, draftFolder);
            } else {
                std::cout << "SELF_MAIL_TEST_OK: one self-addressed test message and one draft remain on the server. No mail was deleted." << std::endl;
                finish(0);
            }
        });
    QTimer::singleShot((selfMailTest ? 10 : 7) * 60 * 1000, &app, [&] {
        std::cout << "PROBE_TIMEOUT" << std::endl;
        finish(1);
    });
    QTimer::singleShot(0, &app, [&] {
        std::cout << (selfMailTest
            ? "Starting live self-mail test: ONE self-addressed send, ONE draft append, INBOX/drafts read in memory. No content logging, token persistence or deletion."
            : "Starting live OAuth probe. No mail will be read or sent.") << std::endl;
        authorization.authorize(QStringLiteral("google"), clientId, email, secret);
    });
    const int result = app.exec();
    if (!completed) {
        std::cout << "PROBE_INCOMPLETE: no terminal authentication result received." << std::endl;
        return 1;
    }
    return result;
}
