#include "mailservice.h"
#include "mimeparser.h"
#include "core/diagnostics.h"
#include "storage/accountrepository.h"

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
#include <KIMAP/StoreJob>
#include <KIMAP/SearchJob>
#include <KIMAP/MoveJob>
#include <KMime/Headers>
#include <KSMTP/LoginJob>
#include <KSMTP/Session>
#include <KSMTP/SendJob>
#include <KLocalizedString>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMutex>
#include <QSet>
#include <QTimer>
#include <QSslSocket>
#include <functional>
extern "C" {
#include <sasl/sasl.h>
}

namespace smoothmail {
namespace {
thread_local const std::atomic_bool *syncCancellation = nullptr;
bool interrupted() { return QThread::currentThread()->isInterruptionRequested() || (syncCancellation && syncCancellation->load()); }
struct CancellationScope {
    explicit CancellationScope(const std::atomic_bool *flag) { syncCancellation = flag; }
    ~CancellationScope() { syncCancellation = nullptr; }
};
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
    if (interrupted()) return QStringLiteral("中断しました。");
    job.setAutoDelete(false);
    QEventLoop loop;
    QTimer timer;
    QElapsedTimer elapsed;
    bool completed = false;
    QObject::connect(&job, &KJob::result, &loop, [&] { completed = true; loop.quit(); });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (elapsed.elapsed() >= 30000 || interrupted()) loop.quit();
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
        if (elapsed.elapsed() >= 30000 || interrupted()) loop.quit();
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

void MailService::synchronize(const Account &account, const QString &secret, const QString &requestedFolder, AccountRepository *cache, SyncMode mode)
{
    if (m_busy) return;
    m_busy = true;
    m_syncAccount = account.id; m_cancelSync = false;
    QMetaObject::invokeMethod(m_worker, [this, account, secret, requestedFolder, cache, mode] {
        const CancellationScope cancellation(&m_cancelSync);
        diagnostics::event("sync-start");
        auto error = validateAccount(account, false);
        if (error.isEmpty()) error = initializeSasl(m_pluginDirectory);
        if (error.isEmpty() && cache) error = cache->prepareSync(account);
        if (interrupted()) error = QStringLiteral("中断しました。");
        QList<MailFolder> folders;
        QList<MailRecord> records;
        QString selected = requestedFolder;
        if (error.isEmpty()) error = testImap(account.imap, secret, [&](KIMAP::Session &session) -> QString {
            diagnostics::event("sync-list");
            const auto listFolders = [&]() -> QString {
            folders.clear();
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
            return {};
            };
            qint64 folderThrough = 0, folderAfter = 0;
            if (cache) { const auto error = cache->folderRevision(account.id, folderThrough); if (!error.isEmpty()) return error; }
            auto jobError = listFolders(); if (!jobError.isEmpty()) return jobError;
            const auto containsFolder = [&](const QString &name) { for (const auto &folder : folders) if (folder.name == name) return true; return false; };
            QList<PendingFolder> operations;
            if (cache) { jobError = cache->pendingFolders(account.id, folderAfter, folderThrough, operations); if (!jobError.isEmpty()) return jobError; }
            while (!operations.isEmpty()) {
                for (const auto &op : operations) {
                    if (interrupted()) return QStringLiteral("中断しました。");
                    bool renamed = false;
                    if (op.operation == QStringLiteral("create") && !containsFolder(op.destination)) {
                        KIMAP::CreateJob job(&session); job.setMailBox(op.destination); jobError = runJob(job);
                    } else if (op.operation == QStringLiteral("rename") && containsFolder(op.source) && !containsFolder(op.destination)) {
                        KIMAP::RenameJob job(&session); job.setSourceMailBox(op.source); job.setDestinationMailBox(op.destination); jobError = runJob(job); renamed = jobError.isEmpty();
                    } else if ((op.operation == QStringLiteral("delete") || op.operation == QStringLiteral("delete-empty")) && containsFolder(op.source)) {
                        if (op.operation == QStringLiteral("delete-empty")) {
                            for (const auto &folder : folders) if (!op.separator.isNull() && folder.name.startsWith(op.source + op.separator)) return QStringLiteral("子フォルダがあるため、作成の取り消しでは削除しません。");
                            KIMAP::SelectJob check(&session); check.setMailBox(op.source); check.setOpenReadOnly(true); jobError = runJob(check);
                            if (!jobError.isEmpty()) return jobError;
                            if (check.messageCount() != 0) return QStringLiteral("フォルダが空でないため、作成の取り消しでは削除しません。");
                        }
                        KIMAP::DeleteJob job(&session); job.setMailBox(op.source); jobError = runJob(job);
                    }
                    if (!jobError.isEmpty()) return jobError;
                    jobError = listFolders(); if (!jobError.isEmpty()) return jobError;
                    // Ambiguous retries/conflicting server paths must not carry
                    // cached UIDs/flag edits into a possibly different mailbox.
                    renamed = renamed && !containsFolder(op.source) && containsFolder(op.destination);
                    jobError = cache->finishFolder(op, renamed, folders); if (!jobError.isEmpty()) return jobError;
                    folderAfter = op.revision;
                }
                jobError = cache->pendingFolders(account.id, folderAfter, folderThrough, operations); if (!jobError.isEmpty()) return jobError;
            }
            bool forceFull = mode == SyncMode::All;
            qint64 moveThrough = 0, moveAfter = 0; QList<PendingMove> moves;
            if (cache) { jobError = cache->moveRevision(account.id, moveThrough); if (jobError.isEmpty()) jobError = cache->pendingMoves(account.id, 0, moveThrough, moves); if (!jobError.isEmpty()) return jobError; }
            if (!moves.isEmpty()) {
                KIMAP::CapabilitiesJob capabilities(&session); jobError = runJob(capabilities); if (!jobError.isEmpty()) return jobError;
                if (!capabilities.capabilities().contains(QStringLiteral("MOVE")) || !capabilities.capabilities().contains(QStringLiteral("UIDPLUS"))) return QStringLiteral("安全なメール移動にはサーバのMOVEとUIDPLUS対応が必要です。保留操作は元に戻せます。");
            }
            while (!moves.isEmpty()) {
                for (const auto &move : moves) {
                    if (interrupted()) return QStringLiteral("中断しました。");
                    if (move.destination == move.identity.folder) { jobError = cache->abandonMove(move.revision); if (!jobError.isEmpty()) return jobError; moveAfter = move.revision; continue; }
                    KIMAP::SelectJob destination(&session); destination.setMailBox(move.destination); destination.setOpenReadOnly(true); jobError = runJob(destination); if (!jobError.isEmpty()) return jobError;
                    const auto targetValidity = destination.uidValidity();
                    KIMAP::SelectJob source(&session); source.setMailBox(move.identity.folder); jobError = runJob(source); if (!jobError.isEmpty()) return jobError;
                    if (source.uidValidity() != move.identity.uidValidity) { jobError = cache->abandonMove(move.revision); if (!jobError.isEmpty()) return jobError; forceFull = true; moveAfter = move.revision; continue; }
                    bool present = false;
                    KIMAP::FetchJob check(&session); check.setUidBased(true); check.setSequenceSet(KIMAP::ImapSet(move.identity.uid)); KIMAP::FetchJob::FetchScope flags; flags.mode = KIMAP::FetchJob::FetchScope::Flags; check.setScope(flags);
                    connect(&check, &KIMAP::FetchJob::messagesAvailable, &check, [&](const QMap<qint64, KIMAP::Message> &items) { for (const auto &item : items) if (item.uid == move.identity.uid) present = true; });
                    jobError = runJob(check); if (!jobError.isEmpty()) return jobError;
                    if (!present) { jobError = cache->abandonMove(move.revision); if (!jobError.isEmpty()) return jobError; forceFull = true; moveAfter = move.revision; continue; }
                    jobError = cache->markMove(move.revision); if (!jobError.isEmpty()) return jobError;
                    KIMAP::MoveJob moving(&session); moving.setUidBased(true); moving.setSequenceSet(KIMAP::ImapSet(move.identity.uid)); moving.setMailBox(move.destination); jobError = runJob(moving); if (!jobError.isEmpty()) return jobError;
                    const auto intervals = moving.resultingUids().intervals();
                    if (intervals.size() != 1 || intervals.first().begin() != intervals.first().end() || intervals.first().begin() <= 0) return QStringLiteral("移動後のUIDを確認できません。次回取得でサーバ状態を確認します。移動は盲目的に再送しません。");
                    KIMAP::SelectJob verify(&session); verify.setMailBox(move.destination); verify.setOpenReadOnly(true); jobError = runJob(verify); if (!jobError.isEmpty()) return jobError;
                    if (verify.uidValidity() != targetValidity) return QStringLiteral("移動先のUIDVALIDITYが変化しました。再取得してください。");
                    jobError = cache->finishMove(move, targetValidity, intervals.first().begin()); if (!jobError.isEmpty()) return jobError; forceFull = true; moveAfter = move.revision;
                }
                jobError = cache->pendingMoves(account.id, moveAfter, moveThrough, moves); if (!jobError.isEmpty()) return jobError;
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
            if (selected.isEmpty()) {
                if (!cache) return {};
                auto error = cache->beginSync(account.id, {}, 0);
                return error.isEmpty() ? cache->commitSync(folders, 0) : error;
            }
            if (!selectable.contains(selected)) {
                if (mode == SyncMode::SelectedFolder) return QStringLiteral("選択フォルダが見つからないか、選択できません。");
                selected = selectable.contains(QStringLiteral("INBOX")) ? QStringLiteral("INBOX") : selectable.value(0);
            }
            const auto visibleFolder = selected;
            const auto targets = mode == SyncMode::SelectedFolder && !forceFull ? QStringList{selected} : selectable;
            if (!cache && mode != SyncMode::SelectedFolder) return QStringLiteral("全フォルダ同期にはローカル保存先が必要です。");
            for (const auto &target : targets) {
            selected = target;
            qint64 through = 0, after = 0;
            QList<PendingFlag> edits;
            if (cache) {
                jobError = cache->flagRevision(through); if (!jobError.isEmpty()) return jobError;
                jobError = cache->pendingFlags(account.id, selected, after, through, edits); if (!jobError.isEmpty()) return jobError;
            }
            KIMAP::SelectJob select(&session);
            diagnostics::event("sync-select");
            select.setMailBox(selected);
            select.setOpenReadOnly(edits.isEmpty()); // No CLOSE/EXPUNGE; write only explicitly queued flag edits.
            jobError = runJob(select);
            if (!jobError.isEmpty()) return jobError;
            if (select.uidValidity() <= 0) return QStringLiteral("UIDVALIDITYが不正です。");
            const qint64 total = select.messageCount();
            const auto validity = select.uidValidity(), nextUid = select.nextUid();
            while (!edits.isEmpty()) {
                if (interrupted()) return QStringLiteral("中断しました。");
                // Coalesce each bounded outbox batch into at most four UID STOREs.
                for (int group = 0; group < 4; ++group) {
                    KIMAP::ImapSet ids; QList<PendingFlag> submitted;
                    for (const auto &edit : edits) if (edit.identity.uidValidity == validity
                        && int(edit.importantFlag) * 2 + int(edit.enabled) == group) { ids.add(edit.identity.uid); submitted.append(edit); }
                    if (submitted.isEmpty()) continue;
                    const QByteArray flag = group >= 2 ? QByteArray("\\Flagged") : QByteArray("\\Seen");
                    KIMAP::StoreJob store(&session); store.setUidBased(true); store.setSequenceSet(ids);
                    store.setMode(group & 1 ? KIMAP::StoreJob::AppendFlags : KIMAP::StoreJob::RemoveFlags); store.setFlags({flag});
                    jobError = runJob(store); if (!jobError.isEmpty()) return jobError;
                    QMap<qint64, KIMAP::Message> confirmed;
                    KIMAP::FetchJob readback(&session); readback.setUidBased(true); readback.setSequenceSet(ids);
                    KIMAP::FetchJob::FetchScope scope; scope.mode = KIMAP::FetchJob::FetchScope::Flags; readback.setScope(scope);
                    connect(&readback, &KIMAP::FetchJob::messagesAvailable, &readback, [&](const QMap<qint64, KIMAP::Message> &items) {
                        for (const auto &item : items) confirmed.insert(item.uid, item);
                    });
                    jobError = runJob(readback); if (!jobError.isEmpty()) return jobError;
                    for (const auto &edit : submitted) {
                        const auto flags = confirmed.value(edit.identity.uid).flags;
                        jobError = cache->acknowledgeFlag(edit, flags.contains("\\Seen"), flags.contains("\\Flagged"), confirmed.contains(edit.identity.uid));
                        if (!jobError.isEmpty()) return jobError;
                    }
                }
                after = edits.last().revision;
                jobError = cache->pendingFlags(account.id, selected, after, through, edits); if (!jobError.isEmpty()) return jobError;
            }
            if (cache && nextUid <= 0) return QStringLiteral("安全な分割同期に必要なUIDNEXTを取得できません。");
            // Compatibility snapshot consumers are bounded; the application's
            // streaming path has no total mailbox count/byte cap.
            if (!cache && total > 10000) return QStringLiteral("一括取得の上限（10000件）を超えています。分割同期を使用してください。");
            QDateTime boundary;
            if (cache && mode == SyncMode::Recent && !forceFull) {
                jobError = cache->recentBoundary(account.id, selected, validity, boundary); if (!jobError.isEmpty()) return jobError;
            }
            QList<qint64> recentUids;
            if (boundary.isValid()) {
                KIMAP::SearchJob search(&session); search.setUidBased(true);
                // SENTSINCE ignores timezone offsets. Include the preceding day
                // and apply the exact instant after receiving bounded headers.
                search.setTerm(KIMAP::Term(KIMAP::Term::SentSince, boundary.date().addDays(-1)));
                jobError = runJob(search); if (!jobError.isEmpty()) return jobError;
                recentUids = search.results();
                QSet<qint64> unique;
                for (const auto uid : recentUids) {
                    if (uid <= 0 || uid >= nextUid || unique.contains(uid)) return QStringLiteral("差分検索のUID応答が不正です。");
                    unique.insert(uid);
                }
                if (recentUids.size() > total) return QStringLiteral("差分検索中にメール一覧が変化しました。");
            }
            if (cache) {
                jobError = cache->beginSync(account.id, selected, validity, boundary);
                if (!jobError.isEmpty()) return jobError;
            }
            qint64 retainedBytes = 0, batchBytes = 0, completed = 0;
            QList<MailRecord> batch;
            const qint64 candidates = boundary.isValid() ? recentUids.size() : total;
            for (qint64 first = 1; first <= candidates; first += MailPageSize) {
            if (interrupted()) return QStringLiteral("中断しました。");
            diagnostics::event("sync-header-batch", first);
            QMap<qint64, KIMAP::Message> headers;
            KIMAP::FetchJob headerJob(&session);
            headerJob.setUidBased(boundary.isValid());
            KIMAP::ImapSet window;
            if (boundary.isValid()) { for (qint64 i = first - 1; i < qMin(candidates, first - 1 + MailPageSize); ++i) window.add(recentUids.at(i)); }
            else window = KIMAP::ImapSet(first, qMin(total, first + MailPageSize - 1));
            headerJob.setSequenceSet(window);
            KIMAP::FetchJob::FetchScope headerScope;
            headerScope.mode = KIMAP::FetchJob::FetchScope::Headers;
            headerJob.setScope(headerScope);
            connect(&headerJob, &KIMAP::FetchJob::messagesAvailable, &headerJob,
                [&](const QMap<qint64, KIMAP::Message> &items) {
                    for (const auto &message : items) if (message.uid > 0) headers.insert(message.uid, message);
                });
            jobError = runJob(headerJob);
            if (!jobError.isEmpty()) return jobError;
            if (headers.size() != qMin<qint64>(MailPageSize, candidates - first + 1)) return QStringLiteral("同期中にメール一覧が変化しました。再試行してください。");
            QMap<qint64, KIMAP::Message> pending;
            qint64 pendingBytes = 0;
            const auto fetchPending = [&]() -> QString {
                if (pending.isEmpty()) return {};
                diagnostics::event("sync-fetch-batch", pending.size());
                QMap<qint64, QByteArray> sources; QString transferError;
                KIMAP::ImapSet ids; for (auto it = pending.cbegin(); it != pending.cend(); ++it) ids.add(it.key());
                KIMAP::FetchJob fetch(&session); fetch.setUidBased(true); fetch.setSequenceSet(ids);
                KIMAP::FetchJob::FetchScope scope; scope.mode = KIMAP::FetchJob::FetchScope::Full; fetch.setScope(scope);
                qint64 receivedBytes = 0;
                connect(&fetch, &KIMAP::FetchJob::messagesAvailable, &fetch, [&](const QMap<qint64, KIMAP::Message> &items) {
                    for (const auto &item : items) if (pending.contains(item.uid) && item.message && transferError.isEmpty()) {
                        auto raw = item.message->encodedContent(KMime::NewlineType::CRLF);
                        receivedBytes += raw.size();
                        if (raw.size() > MimeParser::MaxRawBytes || receivedBytes > MimeParser::MaxRawBytes)
                            transferError = QStringLiteral("受信バッチが安全サイズを超えています。");
                        else sources.insert(item.uid, std::move(raw));
                    }
                });
                auto result = runJob(fetch); if (!result.isEmpty()) return result;
                if (!transferError.isEmpty()) return transferError;
                if (sources.size() != pending.size()) return QStringLiteral("同期中にメールが削除されました。再試行してください。");
                for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
                    if (interrupted()) return QStringLiteral("中断しました。");
                    const auto &raw = it.value(); retainedBytes += raw.size();
                    if (!cache && retainedBytes > 128 * 1024 * 1024) return QStringLiteral("一括取得の安全上限（128 MiB）を超えています。");
                    diagnostics::event("sync-mime", raw.size());
                    const auto parsed = MimeParser::parse(raw); if (!parsed.ok()) return parsed.error;
                    const auto &header = pending[it.key()];
                    MailRecord record{account.id, selected, validity, header.uid, header.flags.contains("\\Seen"), header.flags.contains("\\Flagged"), parsed.message};
                    if (cache) {
                        batch.append(record); batchBytes += raw.size();
                        if (batch.size() >= MailPageSize || batchBytes >= 8 * 1024 * 1024) {
                            result = cache->stageMessages(batch); batch.clear(); batchBytes = 0;
                            if (!result.isEmpty()) return result;
                        }
                    } else records.append(record);
                    ++completed;
                }
                pending.clear(); pendingBytes = 0;
                return {};
            };
            for (const auto &header : headers) {
                if (interrupted()) return QStringLiteral("中断しました。");
                if (boundary.isValid() && header.message && header.message->date()->dateTime().isValid()
                    && header.message->date()->dateTime() < boundary) continue;
                if (header.size > MimeParser::MaxRawBytes) return QStringLiteral("メールがMIMEサイズ上限を超えているため、このフォルダを同期できません。");
                constexpr qint64 snapshotLimit = 128 * 1024 * 1024;
                if (!cache && header.size > snapshotLimit - retainedBytes) return QStringLiteral("一括取得の安全上限（128 MiB）を超えています。");
                const bool seen = header.flags.contains("\\Seen"), important = header.flags.contains("\\Flagged");
                if (cache) {
                    bool found = false;
                    jobError = cache->reuseMessage(header.uid, seen, important, found);
                    if (!jobError.isEmpty()) return jobError;
                    if (found) { ++completed; continue; }
                }
                if (!pending.isEmpty() && (header.size <= 0 || pendingBytes + header.size > 8 * 1024 * 1024)) {
                    jobError = fetchPending(); if (!jobError.isEmpty()) return jobError;
                }
                pending.insert(header.uid, header); pendingBytes += qMax<qint64>(1, header.size);
                if (header.size <= 0 || pendingBytes >= 8 * 1024 * 1024) { jobError = fetchPending(); if (!jobError.isEmpty()) return jobError; }
            }
            jobError = fetchPending(); if (!jobError.isEmpty()) return jobError;
            if (cache && !batch.isEmpty()) {
                jobError = cache->stageMessages(batch); batch.clear(); batchBytes = 0;
                if (!jobError.isEmpty()) return jobError;
            }
            emit syncProgress(account.id, completed, total);
            }
            // Sequence windows can shift on EXPUNGE. Never publish a mixed
            // snapshot: re-check mailbox identity/count/high-water mark.
            KIMAP::SelectJob verify(&session); verify.setMailBox(selected); verify.setOpenReadOnly(true);
            jobError = runJob(verify); if (!jobError.isEmpty()) return jobError;
            if (verify.uidValidity() != validity || verify.messageCount() != total || verify.nextUid() != nextUid)
                return QStringLiteral("同期中にメールボックスが変化しました。再試行してください。");
            if (cache) {
                if (interrupted()) return QStringLiteral("中断しました。");
                jobError = cache->commitSync(folders, boundary.isValid() ? -1 : total);
                cache->abortSync();
                if (!jobError.isEmpty()) return jobError;
            }
            }
            selected = visibleFolder;
            return {};
        });
        if (cache) cache->abortSync(); // Drop disk-backed staging after success or failure.
        // A failed snapshot must never replace previously displayed/cached mail.
        if (!error.isEmpty()) records.clear();
        diagnostics::event(error.isEmpty() ? "sync-complete" : "sync-failed", records.size());
        QMetaObject::invokeMethod(this, [this, account, selected, folders, records, error] {
            m_busy = false; m_syncAccount.clear();
            emit synchronized(account.id, selected, folders, records, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MailService::cancelSynchronization(const QString &id)
{
    if (m_syncAccount == id) m_cancelSync = true;
}

void MailService::setMessageFlag(const Account &account, const QString &secret, const MailRecord &identity, bool importantFlag, bool enabled)
{
    if (m_busy) return;
    m_busy = true;
    QMetaObject::invokeMethod(m_worker, [this, account, secret, identity, importantFlag, enabled] {
        auto error = initializeSasl(m_pluginDirectory);
        if (error.isEmpty()) error = testImap(account.imap, secret, [&](KIMAP::Session &session) -> QString {
            KIMAP::SelectJob select(&session); select.setMailBox(identity.folder);
            auto result = runJob(select); if (!result.isEmpty()) return result;
            if (identity.accountId != account.id || identity.uid <= 0 || select.uidValidity() != identity.uidValidity)
                return QStringLiteral("UIDVALIDITYが変化しました。更新してから再試行してください。");
            KIMAP::StoreJob store(&session); store.setUidBased(true); store.setSequenceSet(KIMAP::ImapSet(identity.uid));
            store.setMode(enabled ? KIMAP::StoreJob::AppendFlags : KIMAP::StoreJob::RemoveFlags);
            store.setFlags({importantFlag ? QByteArray("\\Flagged") : QByteArray("\\Seen")});
            result = runJob(store);
            // A tagged OK can also mean that the UID no longer exists.
            if (result.isEmpty() && store.resultingFlags().isEmpty()) return QStringLiteral("フラグ変更対象のメールが見つかりません。");
            return result;
        });
        QMetaObject::invokeMethod(this, [this, identity, importantFlag, enabled, error] {
            m_busy = false; emit messageFlagChanged(identity, importantFlag, enabled, error);
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
