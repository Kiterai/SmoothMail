#include "mailservice.h"
#include "mimeparser.h"
#include "core/diagnostics.h"

#include <KIMAP/CapabilitiesJob>
#include <KIMAP/LoginJob>
#include <KIMAP/Session>
#include <KIMAP/ListJob>
#include <KIMAP/SelectJob>
#include <KIMAP/StatusJob>
#include <KIMAP/FetchJob>
#include <KIMAP/AppendJob>
#include <KIMAP/CreateJob>
#include <KIMAP/RenameJob>
#include <KIMAP/DeleteJob>
#include <KSMTP/LoginJob>
#include <KSMTP/Session>
#include <KSMTP/SendJob>
#include <KLocalizedString>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMutex>
#include <QTimer>
#include <QSslSocket>
#include <functional>
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
    if (!completed) { diagnostics::event("job-timeout-or-interrupted"); return QStringLiteral("接続処理がタイムアウトまたは中断されました。"); }
    if (job.error()) diagnostics::event("job-error", job.error());
    if (job.error()) return job.errorText().isEmpty() ? QStringLiteral("認証に失敗しました。") : job.errorText().left(1024);
    return {};
}

template<typename Base> class RejectCertificateErrors final : public Base
{
public:
    bool rejected = false;
    bool ignoreSslError(const KSslErrorUiData &) override { rejected = true; return false; }
};

QString testSmtp(const ServerSettings &settings, const QString &secret,
                 const std::function<QString(KSmtp::Session &)> &work = {})
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
    if (work) {
        const auto workError = work(session);
        session.quit();
        return workError;
    }
    session.quit();
    return {};
}

QString testImap(const ServerSettings &settings, const QString &secret,
                 const std::function<QString(KIMAP::Session &)> &work = {})
{
    diagnostics::event("imap-auth-start");
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
    diagnostics::event("imap-auth-complete");
    if (work) {
        const auto workError = work(session);
        session.close();
        return workError;
    }
    session.close();
    return {};
}
}

MailService::MailService(QString plugins, QObject *parent)
    : QObject(parent), m_worker(new QObject), m_pluginDirectory(std::move(plugins))
{
    // KI18n installs an application event filter on first use. Initialize it
    // on the owning (GUI) thread, not from a mail worker's error path.
    KLocalizedString::setApplicationDomain("smoothmail");
    qRegisterMetaType<QList<MailFolder>>();
    qRegisterMetaType<QList<MailRecord>>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

MailService::~MailService()
{
    m_thread.requestInterruption();
    m_thread.quit();
    m_thread.wait();
}

void MailService::test(const Account &account, const QString &smtpSecret, const QString &imapSecret)
{
    if (m_busy) return;
    m_busy = true;
    QMetaObject::invokeMethod(m_worker, [this, account, smtpSecret, imapSecret] {
        const auto deliver = [this](const QString &smtp, const QString &imap) {
            QMetaObject::invokeMethod(this, [this, smtp, imap] {
                m_busy = false;
                emit finished(smtp, imap);
            }, Qt::QueuedConnection);
        };
        auto error = validateAccount(account, false);
        if (error.isEmpty()) error = initializeSasl(m_pluginDirectory);
        if (!error.isEmpty()) { deliver(error, error); return; }
        const auto smtpError = testSmtp(account.smtp, smtpSecret);
        const auto imapError = m_thread.isInterruptionRequested() ? QStringLiteral("中断しました。")
                                                               : testImap(account.imap, imapSecret);
        deliver(smtpError, imapError);
    }, Qt::QueuedConnection);
}

void MailService::synchronize(const Account &account, const QString &secret, const QString &requestedFolder)
{
    if (m_busy) return;
    m_busy = true;
    QMetaObject::invokeMethod(m_worker, [this, account, secret, requestedFolder] {
        diagnostics::event("sync-start");
        auto error = validateAccount(account, false);
        if (error.isEmpty()) error = initializeSasl(m_pluginDirectory);
        QList<MailFolder> folders;
        QList<MailRecord> records;
        QString selected = requestedFolder;
        if (error.isEmpty()) error = testImap(account.imap, secret, [&](KIMAP::Session &session) -> QString {
            diagnostics::event("sync-list");
            KIMAP::ListJob listing(&session);
            listing.setOption(KIMAP::ListJob::IncludeUnsubscribed);
            connect(&listing, &KIMAP::ListJob::mailBoxesReceived, &listing,
                [&](const QList<KIMAP::MailBoxDescriptor> &boxes, const QList<QList<QByteArray>> &flags) {
                    for (qsizetype i = 0; i < boxes.size(); ++i) {
                        folders.append({boxes.at(i).name, boxes.at(i).separator, flags.value(i), 0});
                    }
                });
            auto jobError = runJob(listing);
            if (!jobError.isEmpty()) return jobError;
            if (folders.size() > 10000) return QStringLiteral("フォルダ数が安全上限を超えています。");
            for (const auto &folder : folders) {
                if (folder.name.size() > 4096 || (!folder.separator.isNull() && folder.name.count(folder.separator) > 64))
                    return QStringLiteral("フォルダの階層または名前が安全上限を超えています。");
            }
            QStringList selectable;
            for (auto &folder : folders) {
                diagnostics::event("sync-status");
                bool noSelect = false;
                for (const auto &flag : folder.flags) if (flag.toUpper() == "\\NOSELECT") noSelect = true;
                if (noSelect) continue;
                selectable.append(folder.name);
                KIMAP::StatusJob status(&session);
                status.setMailBox(folder.name);
                status.setDataItems({"UNSEEN"});
                jobError = runJob(status);
                if (!jobError.isEmpty()) return jobError;
                for (const auto &item : status.status()) if (item.first == "UNSEEN") folder.unread = item.second;
            }
            if (selected.isEmpty()) selected = selectable.contains(QStringLiteral("INBOX")) ? QStringLiteral("INBOX") : selectable.value(0);
            if (selected.isEmpty()) return {};
            if (!selectable.contains(selected)) return QStringLiteral("選択フォルダが見つからないか、選択できません。");
            KIMAP::SelectJob select(&session);
            diagnostics::event("sync-select");
            select.setMailBox(selected);
            select.setOpenReadOnly(true); // Refresh must not modify messages or expunge deletions.
            jobError = runJob(select);
            if (!jobError.isEmpty()) return jobError;
            if (select.uidValidity() <= 0) return QStringLiteral("UIDVALIDITYが不正です。");
            if (!select.messageCount()) return {};
            // Until incremental/on-demand loading exists, fail safely rather
            // than retain an unbounded mailbox in the worker/UI/cache at once.
            if (select.messageCount() > 10000) return QStringLiteral("フォルダの件数が安全上限（10000件）を超えています。分割して同期してください。");
            diagnostics::event("sync-headers", select.messageCount());
            QMap<qint64, KIMAP::Message> headers;
            KIMAP::FetchJob headerJob(&session);
            headerJob.setUidBased(true);
            headerJob.setSequenceSet(KIMAP::ImapSet(1, 0));
            KIMAP::FetchJob::FetchScope headerScope;
            headerScope.mode = KIMAP::FetchJob::FetchScope::FullHeaders;
            headerJob.setScope(headerScope);
            connect(&headerJob, &KIMAP::FetchJob::messagesAvailable, &headerJob,
                [&](const QMap<qint64, KIMAP::Message> &items) {
                    for (const auto &message : items) if (message.uid > 0) headers.insert(message.uid, message);
                });
            jobError = runJob(headerJob);
            if (!jobError.isEmpty()) return jobError;
            qint64 retainedBytes = 0;
            for (const auto &header : headers) {
                if (m_thread.isInterruptionRequested()) return QStringLiteral("中断しました。");
                if (header.size > MimeParser::MaxRawBytes) return QStringLiteral("メールがMIMEサイズ上限を超えているため、このフォルダを同期できません。");
                constexpr qint64 snapshotLimit = 128 * 1024 * 1024;
                if (header.size > snapshotLimit - retainedBytes) return QStringLiteral("同期データが安全上限（128 MiB）を超えています。");
                diagnostics::event("sync-fetch", records.size());
                QByteArray raw;
                KIMAP::FetchJob fetch(&session);
                fetch.setUidBased(true);
                fetch.setSequenceSet(KIMAP::ImapSet(header.uid));
                KIMAP::FetchJob::FetchScope scope;
                scope.mode = KIMAP::FetchJob::FetchScope::Full;
                fetch.setScope(scope);
                connect(&fetch, &KIMAP::FetchJob::messagesAvailable, &fetch,
                    [&](const QMap<qint64, KIMAP::Message> &items) {
                        for (const auto &item : items) {
                            if (item.uid == header.uid && item.message) raw = item.message->encodedContent(KMime::NewlineType::CRLF);
                        }
                    });
                jobError = runJob(fetch);
                if (!jobError.isEmpty()) return jobError;
                if (raw.isEmpty()) continue; // The message may have been expunged during this refresh.
                retainedBytes += raw.size();
                if (retainedBytes > snapshotLimit) return QStringLiteral("同期データが安全上限（128 MiB）を超えています。");
                diagnostics::event("sync-mime", raw.size());
                const auto parsed = MimeParser::parse(raw);
                if (!parsed.ok()) return parsed.error;
                records.append({account.id, selected, select.uidValidity(), header.uid,
                    header.flags.contains("\\Seen"), header.flags.contains("\\Flagged"), parsed.message});
            }
            return {};
        });
        // A failed snapshot must never replace previously displayed/cached mail.
        if (!error.isEmpty()) records.clear();
        diagnostics::event(error.isEmpty() ? "sync-complete" : "sync-failed", records.size());
        QMetaObject::invokeMethod(this, [this, account, selected, folders, records, error] {
            m_busy = false;
            emit synchronized(account.id, selected, folders, records, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MailService::changeFolder(const Account &account, const QString &secret, const QString &operation,
                              const QString &source, const QString &destination)
{
    if (m_busy) return;
    m_busy = true;
    QMetaObject::invokeMethod(m_worker, [this, account, secret, operation, source, destination] {
        auto error = validateAccount(account, false);
        if (error.isEmpty()) error = initializeSasl(m_pluginDirectory);
        if (operation != QStringLiteral("create") && operation != QStringLiteral("rename") && operation != QStringLiteral("delete")) error = QStringLiteral("不正な操作です。");
        const auto validName = [](const QString &name) {
            return !name.isEmpty() && name.size() <= 4096 && !name.contains(u'\r') && !name.contains(u'\n') && !name.contains(QChar::Null);
        };
        if ((operation != QStringLiteral("create") && !validName(source))
            || (operation != QStringLiteral("delete") && !validName(destination))) error = QStringLiteral("フォルダ名が不正です。");
        if (error.isEmpty()) error = testImap(account.imap, secret, [&](KIMAP::Session &session) -> QString {
            diagnostics::event("folder-operation");
            if (operation == QStringLiteral("create")) {
                KIMAP::CreateJob job(&session); job.setMailBox(destination); return runJob(job);
            }
            if (operation == QStringLiteral("rename")) {
                KIMAP::RenameJob job(&session); job.setSourceMailBox(source); job.setDestinationMailBox(destination); return runJob(job);
            }
            KIMAP::DeleteJob job(&session); job.setMailBox(source); return runJob(job);
        });
        QMetaObject::invokeMethod(this, [this, account, error] {
            m_busy = false; emit folderChanged(account.id, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MailService::submit(const Account &account, const QString &secret, const ComposeData &draft, bool saveAsDraft)
{
    if (m_busy) return;
    m_busy = true;
    QMetaObject::invokeMethod(m_worker, [this, account, secret, draft, saveAsDraft] {
        auto error = validateAccount(account, false);
        if (error.isEmpty()) error = initializeSasl(m_pluginDirectory);
        if (draft.from != account.email) error = QStringLiteral("送信元が選択アカウントと一致しません。");
        ComposedMessage message;
        if (error.isEmpty()) { message = composeMessage(draft, saveAsDraft); error = message.error; }
        if (error.isEmpty() && !saveAsDraft) {
            error = testSmtp(account.smtp, secret, [&](KSmtp::Session &session) {
                KSmtp::SendJob send(&session);
                send.setFrom(message.from);
                send.setTo(message.to); send.setCc(message.cc); send.setBcc(message.bcc);
                send.setData(message.transportSource);
                // Never retry automatically: a connection loss after DATA may
                // occur after the server has already accepted the message.
                return runJob(send);
            });
        } else if (error.isEmpty()) {
            error = testImap(account.imap, secret, [&](KIMAP::Session &session) -> QString {
                QString folder;
                bool standardNameExists = false;
                KIMAP::ListJob listing(&session);
                listing.setOption(KIMAP::ListJob::IncludeUnsubscribed);
                connect(&listing, &KIMAP::ListJob::mailBoxesReceived, &listing,
                    [&](const QList<KIMAP::MailBoxDescriptor> &boxes, const QList<QList<QByteArray>> &flags) {
                        for (qsizetype i = 0; i < boxes.size(); ++i) {
                            bool selectable = true, drafts = false;
                            for (const auto &flag : flags.value(i)) {
                                if (flag.toUpper() == "\\NOSELECT") selectable = false;
                                if (flag.toUpper() == "\\DRAFTS") drafts = true;
                            }
                            if (selectable && drafts && folder.isEmpty()) folder = boxes.at(i).name;
                            if (selectable && boxes.at(i).name == QStringLiteral("Drafts")) standardNameExists = true;
                        }
                    });
                auto jobError = runJob(listing);
                if (!jobError.isEmpty()) return jobError;
                if (folder.isEmpty() && standardNameExists) folder = QStringLiteral("Drafts");
                if (folder.isEmpty()) return QStringLiteral("下書きフォルダを特定できませんでした。サーバの下書きフォルダ設定を確認してください。");
                KIMAP::AppendJob append(&session);
                append.setMailBox(folder);
                append.setFlags({"\\Draft", "\\Seen"});
                append.setContent(message.storageSource);
                return runJob(append);
            });
        }
        QMetaObject::invokeMethod(this, [this, account, saveAsDraft, error] {
            m_busy = false;
            emit submissionFinished(account.id, saveAsDraft, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
} // namespace smoothmail
