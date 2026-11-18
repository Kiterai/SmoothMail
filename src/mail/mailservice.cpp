#include "mailservice.h"
#include "mime/mimeparser.h"
#include "connectionpolicy.h"
#include "syncbudget.h"
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
#include <QHash>
#include <QTemporaryFile>
#include <QDataStream>
#include <QTimer>
#include <QSslSocket>
#include <functional>
#include <algorithm>
#include <future>
#include <QScopeGuard>
#include <QUuid>
#include <mutex>
#include <vector>
extern "C" {
#include <sasl/sasl.h>
}

namespace smoothmail {
namespace {
bool hasFolderFlag(const QList<QByteArray> &flags, const QByteArray &wanted)
{
    for (const auto &flag : flags) if (flag.compare(wanted, Qt::CaseInsensitive) == 0) return true;
    return false;
}
void mergeFolderFlags(QList<QByteArray> &flags, const QList<QByteArray> &incoming)
{
    // A hierarchy placeholder must not hide a selectable description of the
    // same mailbox. Preserve role/extension flags rather than dropping a row.
    const bool selectable = isSelectableFolder(flags) || isSelectableFolder(incoming);
    for (const auto &flag : incoming) if (!hasFolderFlag(flags, flag)) flags.append(flag);
    if (selectable) flags.removeIf([](const QByteArray &flag) {
        return flag.toUpper() == "\\NOSELECT" || flag.toUpper() == "\\NONEXISTENT";
    });
    if (hasFolderFlag(flags, "\\HasChildren")) flags.removeIf([](const QByteArray &flag) {
        const auto upper = flag.toUpper(); return upper == "\\HASNOCHILDREN" || upper == "\\NOINFERIORS";
    });
}
thread_local const std::atomic_bool *syncCancellation = nullptr;
bool interrupted() { return QThread::currentThread()->isInterruptionRequested() || (syncCancellation && syncCancellation->load()); }
struct CancellationScope {
    const std::atomic_bool *previous;
    explicit CancellationScope(const std::atomic_bool *flag) : previous(syncCancellation) { syncCancellation = flag; }
    ~CancellationScope() { syncCancellation = previous; }
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
    m_cancelSync = true; // Also wake the bounded ingestion consumer.
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

void MailService::synchronize(const Account &account, const QString &secret, const QString &requestedFolder, AccountRepository *cache, SyncMode mode,
                              std::shared_ptr<SyncBudget> budget)
{
    if (m_busy) return;
    m_busy = true;
    m_syncDemand = 1;
    // An external budget already reserved the primary before acquiring secrets.
    if (!budget) { budget = std::make_shared<SyncBudget>(m_syncConcurrency.load()); budget->reserve(); }
    m_syncAccount = account.id; m_cancelSync = false;
    QMetaObject::invokeMethod(m_worker, [this, account, secret, requestedFolder, cache, mode, budget] {
        const auto primarySlot = qScopeGuard([&] { budget->release(); emit syncCapacityChanged(); });
        const diagnostics::OperationScope operation(diagnostics::nextOperation());
        const CancellationScope cancellation(&m_cancelSync);
        diagnostics::event("sync-start");
        diagnostics::event("sync-mode", int(mode));
        auto error = validateAccount(account, false);
        if (error.isEmpty()) error = initializeSasl(m_pluginDirectory);
        if (error.isEmpty() && cache) error = cache->prepareSync(account);
        if (interrupted()) error = QStringLiteral("中断しました。");
        QList<MailFolder> folders;
        QList<MailRecord> records;
        QString selected = requestedFolder;
        const bool singleFolder = !requestedFolder.isEmpty() && (mode == SyncMode::SelectedFolder || mode == SyncMode::SelectedRecent);
        if (error.isEmpty() && mode == SyncMode::SelectedRecent && requestedFolder.isEmpty()) error = QStringLiteral("更新対象のフォルダがありません。");
        if (error.isEmpty() && singleFolder && cache) {
            bool exists = true; error = cache->resolveSyncFolder(account.id, requestedFolder, selected, exists);
            if (error.isEmpty() && !exists) error = QStringLiteral("このフォルダはサーバに未作成です。アカウントを更新して保留操作を反映してください。");
        }
        if (error.isEmpty()) {
        int setupFailures = 0;
        bool folderPhaseStarted = false;
        do {
        error = testImap(account.imap, secret, [&](KIMAP::Session &session) -> QString {
            diagnostics::event("sync-list");
            bool listStatusEnabled = false;
            QHash<QString, qint64> listedUnread;
            std::function<QString()> listFolders;
            listFolders = [&]() -> QString {
            folders.clear();
            listedUnread.clear();
            struct ListedFolder { qsizetype position; qint64 firstResponse; };
            QHash<QString, ListedFolder> listed;
            qint64 rawCount = 0, duplicates = 0, firstDuplicate = 0, duplicateOf = 0;
            QString listError;
            KIMAP::ListJob listing(&session);
            listing.setOption(KIMAP::ListJob::IncludeUnsubscribed);
            if (listStatusEnabled) {
                listing.setListExtendedEnabled(true);
                KIMAP::ListReturnOptions::Status status; status.unseen = true;
                listing.setReturnOptions(status);
                connect(&listing, &KIMAP::ListJob::mailBoxesStatusReceived, &listing,
                    [&](const QList<KIMAP::MailBoxDescriptor> &boxes, const QList<QList<QByteArray>> &,
                        const QList<std::optional<KIMAP::ListJob::MailboxStatus>> &statuses) {
                        for (qsizetype i = 0; i < qMin(boxes.size(), statuses.size()); ++i) {
                            if (!statuses[i]) continue;
                            for (const auto &item : *statuses[i])
                                if (item.first == "UNSEEN" && item.second >= 0) listedUnread.insert(boxes[i].name, item.second);
                        }
                    });
            }
            connect(&listing, &KIMAP::ListJob::mailBoxesReceived, &listing,
                [&](const QList<KIMAP::MailBoxDescriptor> &boxes, const QList<QList<QByteArray>> &flags) {
                    diagnostics::event("sync-list-batch", boxes.size());
                    if (boxes.size() != flags.size()) diagnostics::event("sync-list-flags-count-mismatch", flags.size());
                    for (qsizetype i = 0; i < boxes.size(); ++i) {
                        ++rawCount;
                        if (!listError.isEmpty()) continue;
                        if (rawCount > 10000) { listError = QStringLiteral("フォルダ数が安全上限を超えています。"); continue; }
                        const auto &box = boxes.at(i);
                        const auto found = listed.constFind(box.name);
                        if (!box.name.isEmpty() && found != listed.cend()) {
                            if (folders.at(found->position).separator != box.separator) {
                                diagnostics::event("sync-list-separator-conflict", rawCount);
                                diagnostics::event("sync-list-conflict-of-index", found->firstResponse);
                                diagnostics::event("sync-list-first-separator", folders.at(found->position).separator.unicode());
                                diagnostics::event("sync-list-conflicting-separator", box.separator.unicode());
                                listError = QStringLiteral("同名フォルダの階層区切りが一致しません。"); continue;
                            }
                            if (!duplicates++) { firstDuplicate = rawCount; duplicateOf = found->firstResponse; }
                            mergeFolderFlags(folders[found->position].flags, flags.value(i));
                        } else {
                            // Empty names remain invalid; do not disguise them
                            // as a successfully normalized mailbox.
                            if (!box.name.isEmpty()) listed.insert(box.name, {folders.size(), rawCount});
                            folders.append({box.name, box.separator, flags.value(i), 0});
                        }
                    }
                });
            auto jobError = runJob(listing);
            if (!jobError.isEmpty()) {
                if (listStatusEnabled && listing.error()
                    && (session.state() == KIMAP::Session::Authenticated || session.state() == KIMAP::Session::Selected)
                    && !interrupted()) {
                    listStatusEnabled = false;
                    return listFolders(); // One plain LIST retry; never publish the rejected partial list.
                }
                return jobError;
            }
            if (!listing.errorText().isEmpty()) return QStringLiteral("フォルダ一覧の応答が不正です。");
            diagnostics::event("sync-list-raw-total", rawCount);
            if (duplicates) {
                diagnostics::event("sync-list-duplicates-merged", duplicates);
                diagnostics::event("sync-list-first-duplicate-index", firstDuplicate);
                diagnostics::event("sync-list-duplicate-of-index", duplicateOf);
            }
            if (!listError.isEmpty()) return listError;
            diagnostics::event("sync-list-complete", folders.size());
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
            // Don't add a capability round trip to a one-folder incremental
            // sync. For multiple folders the additional LIST is amortized by
            // avoiding an individual STATUS round trip for every mailbox.
            if (cache && !singleFolder && mode != SyncMode::SelectedFolder && folders.size() > 1) {
                KIMAP::CapabilitiesJob capabilities(&session);
                jobError = runJob(capabilities); if (!jobError.isEmpty()) return jobError;
                listStatusEnabled = capabilities.capabilities().contains(QStringLiteral("LIST-STATUS"));
                if (listStatusEnabled) { jobError = listFolders(); if (!jobError.isEmpty()) return jobError; }
            }
            const auto containsFolder = [&](const QString &name) { for (const auto &folder : folders) if (folder.name == name) return true; return false; };
            QList<PendingFolder> operations;
            if (cache && !singleFolder) { jobError = cache->pendingFolders(account.id, folderAfter, folderThrough, operations); if (!jobError.isEmpty()) return jobError; }
            // Acquisition retries must not blindly replay cross-folder changes.
            if (!operations.isEmpty()) folderPhaseStarted = true;
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
            if (cache && !singleFolder) { jobError = cache->moveRevision(account.id, moveThrough); if (jobError.isEmpty()) jobError = cache->pendingMoves(account.id, 0, moveThrough, moves); if (!jobError.isEmpty()) return jobError; }
            if (!moves.isEmpty()) {
                folderPhaseStarted = true;
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
                if (!isSelectableFolder(folder.flags)) continue;
                selectable.append(folder.name);
                if (singleFolder && folder.name != selected) continue;
                if (listedUnread.contains(folder.name)) { folder.unread = listedUnread.value(folder.name); continue; }
                diagnostics::event("sync-status");
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
                auto error = cache->beginSync(account.id, {}, 0, {}, diagnostics::currentOperation());
                return error.isEmpty() ? cache->commitSync(folders, 0, false, account.id) : error;
            }
            if (!selectable.contains(selected)) {
                if (singleFolder || mode == SyncMode::SelectedFolder) return QStringLiteral("選択フォルダが見つからないか、選択できません。");
                selected = selectable.contains(QStringLiteral("INBOX")) ? QStringLiteral("INBOX") : selectable.value(0);
            }
            const auto visibleFolder = selected;
            const auto targets = singleFolder || (mode == SyncMode::SelectedFolder && !forceFull) ? QStringList{selected} : selectable;
            if (!cache && mode != SyncMode::SelectedFolder) return QStringLiteral("全フォルダ同期にはローカル保存先が必要です。");
            m_syncDemand = int(targets.size()); emit syncCapacityChanged();
            folderPhaseStarted = true;
            const auto syncFolder = [&](KIMAP::Session &session, const QString &selected, bool scopedCommit) -> QString {
            const auto progressFinished = qScopeGuard([&] { emit syncFolderFinished(account.id, selected); });
            const auto syncKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const auto staging = qScopeGuard([&] { if (cache) cache->abortSync(syncKey); });
            bool stageStarted = false, boundaryResolved = false, snapshotResolved = false;
            QDateTime boundary;
            qint64 snapshotValidity = 0, snapshotNextUid = 0, nextWindow = 1, fullCompleted = 0;
            QList<qint64> snapshotUids;
            const auto publishedFolders = [&] {
                QList<MailFolder> result;
                if (scopedCommit) {
                    QChar separator;
                    for (const auto &folder : folders) if (folder.name == selected) separator = folder.separator;
                    for (const auto &folder : folders)
                        if (folder.name == selected || (!separator.isNull() && folder.name.startsWith(selected + separator)))
                            result.append(folder);
                }
                else result = folders;
                return result;
            };
            const auto attempt = [&](KIMAP::Session &session) -> QString {
            QString jobError;
            emit syncProgress(account.id, selected, 0, -1);
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
            qint64 total = select.messageCount();
            const auto validity = select.uidValidity();
            if (snapshotResolved && snapshotValidity != validity)
                return QStringLiteral("UIDVALIDITYが変化しました。再度更新してください。");
            const auto nextUid = snapshotResolved ? snapshotNextUid : select.nextUid();
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
            if (!boundaryResolved && cache && (mode == SyncMode::Recent || mode == SyncMode::SelectedRecent) && !forceFull) {
                jobError = cache->recentBoundary(account.id, selected, validity, boundary); if (!jobError.isEmpty()) return jobError;
            }
            boundaryResolved = true; // Do not move the cutoff after publishing a partial result.
            if (!snapshotResolved) {
                snapshotValidity = validity; snapshotNextUid = nextUid;
                if (!boundary.isValid()) {
                    KIMAP::SearchJob search(&session); search.setUidBased(true);
                    search.setTerm(KIMAP::Term(KIMAP::Term::All, QString{}));
                    jobError = runJob(search); if (!jobError.isEmpty()) return jobError;
                    if (!search.errorText().isEmpty()) return QStringLiteral("UID検索の応答が不正です。");
                    snapshotUids = search.results();
                    std::sort(snapshotUids.begin(), snapshotUids.end());
                    qint64 previous = 0;
                    for (const auto uid : snapshotUids) {
                        if (uid <= 0 || uid == previous) return QStringLiteral("UID検索の応答が不正です。");
                        previous = uid;
                    }
                    if (nextUid > 0) snapshotUids.removeIf([&](qint64 uid) { return uid >= nextUid; });
                }
                snapshotResolved = true;
            }
            if (!boundary.isValid()) total = snapshotUids.size();
            QList<qint64> recentUids;
            if (boundary.isValid()) {
                KIMAP::SearchJob search(&session); search.setUidBased(true);
                // SENTSINCE ignores timezone offsets. Include the preceding day
                // and apply the exact instant using immutable cached Dates or
                // bounded headers for UIDs that have not been cached yet.
                search.setTerm(KIMAP::Term(KIMAP::Term::SentSince, boundary.date().addDays(-1)));
                jobError = runJob(search); if (!jobError.isEmpty()) return jobError;
                if (!search.errorText().isEmpty()) return QStringLiteral("差分検索の応答が不正です。");
                recentUids = search.results();
                std::sort(recentUids.begin(), recentUids.end());
                qint64 previous = 0;
                for (const auto uid : recentUids) {
                    if (uid <= 0 || uid == previous) return QStringLiteral("差分検索のUID応答が不正です。");
                    previous = uid;
                }
                recentUids.removeIf([&](qint64 uid) { return uid >= nextUid; });
            }
            if (cache && !stageStarted) {
                jobError = cache->beginSync(account.id, selected, validity, boundary, diagnostics::currentOperation(), syncKey);
                if (!jobError.isEmpty()) return jobError;
                stageStarted = true;
            }
            qint64 retainedBytes = 0, completed = boundary.isValid() ? 0 : fullCompleted;
            // One consumer only: receive the next bounded batch while MIME
            // extraction and the private DB worker stage the previous one.
            // A future joins before any exit can abort this folder's staging.
            std::future<QString> ingestion;
            const auto finishIngestion = qScopeGuard([&] { if (ingestion.valid()) ingestion.wait(); });
            qint64 ingestionCount = 0;
            const auto drain = [&]() -> QString {
                if (!ingestion.valid()) return {};
                QString result;
                try { result = ingestion.get(); }
                catch (...) { return QStringLiteral("メールの解析・保存処理に失敗しました。"); }
                if (result.isEmpty()) completed += ingestionCount;
                ingestionCount = 0;
                return result;
            };
            qint64 candidates = boundary.isValid() ? recentUids.size() : total;
            // Resolve exact-date exclusions and cached bodies before publishing
            // a differential total. Spool only metadata to keep memory bounded.
            QTemporaryFile planned;
            QDataStream plan(&planned);
            if (boundary.isValid()) {
                if (!planned.open()) return QStringLiteral("差分取得の一時ファイルを作成できません。");
                qint64 needed = 0;
                // A large FLAGS response requires one parsed IMAP response per
                // message. Two standard searches return compact UID lists for
                // exactly the two flags we persist, independent of batch count.
                constexpr qint64 flagSearchThreshold = 256;
                bool flagsBySearch = recentUids.size() > flagSearchThreshold;
                QList<qint64> unseenUids, flaggedUids;
                if (flagsBySearch) {
                    bool rejected = false;
                    const auto searchFlags = [&](const KIMAP::Term &flag, QList<qint64> &uids) -> QString {
                        KIMAP::SearchJob search(&session); search.setUidBased(true);
                        search.setTerm(KIMAP::Term(KIMAP::Term::And, {flag,
                            KIMAP::Term(KIMAP::Term::SentSince, boundary.date().addDays(-1))}));
                        const auto error = runJob(search);
                        // KIMAP can report a malformed tagged reply through
                        // errorText without setting error(). Don't interpret
                        // that as a valid empty set of unseen/flagged UIDs.
                        if (error.isEmpty() && !search.errorText().isEmpty()) return QStringLiteral("フラグ検索の応答が不正です。");
                        if (!error.isEmpty()) {
                            rejected = search.error() != 0
                                && (session.state() == KIMAP::Session::Authenticated || session.state() == KIMAP::Session::Selected)
                                && !interrupted();
                            return error;
                        }
                        uids = search.results(); std::sort(uids.begin(), uids.end());
                        qint64 previous = 0;
                        for (const auto uid : uids) {
                            if (uid >= nextUid) continue; // New arrivals belong to the next refresh.
                            if (uid <= 0 || uid == previous
                                || !std::binary_search(recentUids.cbegin(), recentUids.cend(), uid))
                                return QStringLiteral("フラグ検索のUID応答が不正です。");
                            previous = uid;
                        }
                        return {};
                    };
                    auto unseen = KIMAP::Term(KIMAP::Term::Seen); unseen.setNegated(true);
                    jobError = searchFlags(unseen, unseenUids);
                    if (jobError.isEmpty()) jobError = searchFlags(KIMAP::Term(KIMAP::Term::Flagged), flaggedUids);
                    if (!jobError.isEmpty()) {
                        // A server rejecting the standard boolean search may
                        // still support FETCH. Never mask malformed UID results,
                        // timeout, cancellation or a disconnected session.
                        if (!rejected) return jobError;
                        flagsBySearch = false; unseenUids.clear(); flaggedUids.clear();
                        diagnostics::event("sync-flag-search-fallback");
                    }
                }
                // Flags are tiny metadata, not MIME bodies. Larger bounded
                // windows avoid one round trip per 100 unchanged headers.
                constexpr qint64 flagWindow = 1000;
                for (qint64 first = 0; first < recentUids.size(); first += flagWindow) {
                    if (interrupted()) return QStringLiteral("中断しました。");
                    const auto requested = recentUids.mid(first, flagWindow);
                    QMap<qint64, KIMAP::Message> flags;
                    if (flagsBySearch) {
                        for (const auto uid : requested) {
                            KIMAP::Message item; item.uid = uid;
                            if (!std::binary_search(unseenUids.cbegin(), unseenUids.cend(), uid)) item.flags.append("\\Seen");
                            if (std::binary_search(flaggedUids.cbegin(), flaggedUids.cend(), uid)) item.flags.append("\\Flagged");
                            flags.insert(uid, item);
                        }
                    } else {
                        const QSet<qint64> requestedSet(requested.cbegin(), requested.cend());
                        KIMAP::FetchJob job(&session); job.setUidBased(true);
                        KIMAP::ImapSet ids; ids.add(requested);
                        job.setSequenceSet(ids);
                        KIMAP::FetchJob::FetchScope scope; scope.mode = KIMAP::FetchJob::FetchScope::Flags; job.setScope(scope);
                        connect(&job, &KIMAP::FetchJob::messagesAvailable, &job, [&](const QMap<qint64, KIMAP::Message> &items) {
                            for (const auto &item : items) if (requestedSet.contains(item.uid)) flags.insert(item.uid, item);
                        });
                        jobError = runJob(job); if (!jobError.isEmpty()) return jobError;
                        if (flags.size() != requested.size())
                            return QStringLiteral("同期中にメール一覧が変化しました。再試行してください。");
                    }
                    QList<qint64> missing;
                    QList<AccountRepository::SyncFlags> reusable;
                    const auto reuse = [&]() -> QString {
                        QSet<qint64> reused;
                        const auto error = cache->reuseMessages(reusable, reused, syncKey);
                        if (!error.isEmpty()) return error;
                        for (const auto &candidate : reusable) if (!reused.contains(candidate.uid)) missing.append(candidate.uid);
                        reusable.clear();
                        return {};
                    };
                    for (const auto &item : flags) {
                        reusable.append({item.uid, item.flags.contains("\\Seen"), item.flags.contains("\\Flagged")});
                        if (reusable.size() == MailPageSize) {
                            if (interrupted()) return QStringLiteral("中断しました。");
                            jobError = reuse(); if (!jobError.isEmpty()) return jobError;
                        }
                    }
                    if (!reusable.isEmpty()) { jobError = reuse(); if (!jobError.isEmpty()) return jobError; }
                    // Existing UIDs need neither headers nor MIME parsing. New
                    // candidates still get Date/size checks before any body.
                    for (qsizetype offset = 0; offset < missing.size(); offset += MailPageSize) {
                        if (interrupted()) return QStringLiteral("中断しました。");
                        const auto unknown = missing.mid(offset, MailPageSize);
                        const QSet<qint64> unknownSet(unknown.cbegin(), unknown.cend());
                        QMap<qint64, KIMAP::Message> headers;
                        KIMAP::FetchJob headerJob(&session); headerJob.setUidBased(true);
                        KIMAP::ImapSet headerIds; headerIds.add(unknown); headerJob.setSequenceSet(headerIds);
                        KIMAP::FetchJob::FetchScope headerScope; headerScope.mode = KIMAP::FetchJob::FetchScope::Headers; headerJob.setScope(headerScope);
                        connect(&headerJob, &KIMAP::FetchJob::messagesAvailable, &headerJob, [&](const QMap<qint64, KIMAP::Message> &items) {
                            for (const auto &header : items) if (unknownSet.contains(header.uid)) headers.insert(header.uid, header);
                        });
                        jobError = runJob(headerJob); if (!jobError.isEmpty()) return jobError;
                        if (headers.size() != unknown.size()) return QStringLiteral("同期中にメール一覧が変化しました。再試行してください。");
                        for (const auto &header : headers) {
                            if (!header.message || !header.message->date()->dateTime().isValid()
                                || header.message->date()->dateTime() <= boundary) continue;
                            if (header.size > MimeParser::MaxRawBytes) return QStringLiteral("メールがMIMEサイズ上限を超えているため、このフォルダを同期できません。");
                            plan << header.uid << header.size << header.flags;
                            ++needed;
                        }
                    }
                }
                if (plan.status() != QDataStream::Ok || !planned.seek(0)) return QStringLiteral("差分取得の一時ファイルを読み書きできません。");
                candidates = needed;
            }
            const qint64 progressTotal = boundary.isValid() ? candidates : total;
            qint64 receivedDone = boundary.isValid() ? 0 : fullCompleted;
            QTimer progressTimer;
            connect(&progressTimer, &QTimer::timeout, &progressTimer, [&] {
                emit syncProgress(account.id, selected, receivedDone, progressTotal);
            });
            progressTimer.start(3000);
            emit syncProgress(account.id, selected, 0, progressTotal);
            // Header metadata and UI/DB pages have different cost models.
            // Bound metadata to 1000 messages, bodies independently to 8 MiB.
            constexpr qint64 headerWindow = 1000;
            for (qint64 first = boundary.isValid() ? 1 : nextWindow; first <= candidates; first += headerWindow) {
            if (interrupted()) return QStringLiteral("中断しました。");
            diagnostics::event("sync-header-batch", first);
            QMap<qint64, KIMAP::Message> headers;
            if (boundary.isValid()) {
                for (qint64 i = first; i <= qMin(candidates, first + headerWindow - 1); ++i) {
                    KIMAP::Message header;
                    plan >> header.uid >> header.size >> header.flags;
                    headers.insert(header.uid, header);
                }
                if (plan.status() != QDataStream::Ok) return QStringLiteral("差分取得の一時ファイルを読み込めません。");
            } else {
            KIMAP::FetchJob headerJob(&session);
            headerJob.setUidBased(true);
            const auto requested = snapshotUids.mid(first - 1, headerWindow);
            const QSet<qint64> requestedSet(requested.cbegin(), requested.cend());
            KIMAP::ImapSet ids; ids.add(requested);
            headerJob.setSequenceSet(ids);
            KIMAP::FetchJob::FetchScope headerScope;
            headerScope.mode = KIMAP::FetchJob::FetchScope::Headers;
            headerJob.setScope(headerScope);
            connect(&headerJob, &KIMAP::FetchJob::messagesAvailable, &headerJob,
                [&](const QMap<qint64, KIMAP::Message> &items) {
                    for (const auto &message : items) if (requestedSet.contains(message.uid)) headers.insert(message.uid, message);
                });
            jobError = runJob(headerJob);
            if (!jobError.isEmpty()) return jobError;
            // A successful UID FETCH may omit messages expunged since SEARCH.
            // Unlike sequence windows, later windows cannot shift or skip UIDs.
            }
            QSet<qint64> reused;
            if (cache && !boundary.isValid()) {
                QList<AccountRepository::SyncFlags> reusable;
                const auto reuse = [&]() -> QString {
                    QSet<qint64> found;
                    const auto error = cache->reuseMessages(reusable, found, syncKey);
                    reused.unite(found); reusable.clear();
                    return error;
                };
                for (const auto &header : headers) {
                    if (interrupted()) return QStringLiteral("中断しました。");
                    if (header.size > MimeParser::MaxRawBytes) return QStringLiteral("メールがMIMEサイズ上限を超えているため、このフォルダを同期できません。");
                    reusable.append({header.uid, header.flags.contains("\\Seen"), header.flags.contains("\\Flagged")});
                    if (reusable.size() == MailPageSize) { jobError = reuse(); if (!jobError.isEmpty()) return jobError; }
                }
                if (!reusable.isEmpty()) { jobError = reuse(); if (!jobError.isEmpty()) return jobError; }
            }
            QMap<qint64, KIMAP::Message> pending;
            qint64 pendingBytes = 0;
            const auto fetchPending = [&]() -> QString {
                if (pending.isEmpty()) return {};
                diagnostics::event("sync-fetch-batch", pending.size());
                QMap<qint64, QByteArray> sources; QString transferError;
                KIMAP::ImapSet ids; ids.add(pending.keys());
                KIMAP::FetchJob fetch(&session); fetch.setUidBased(true); fetch.setSequenceSet(ids);
                KIMAP::FetchJob::FetchScope scope; scope.mode = KIMAP::FetchJob::FetchScope::Full; fetch.setScope(scope);
                qint64 receivedBytes = 0;
                connect(&fetch, &KIMAP::FetchJob::messagesAvailable, &fetch, [&](const QMap<qint64, KIMAP::Message> &items) {
                    for (const auto &item : items) if (pending.contains(item.uid) && item.message && transferError.isEmpty()) {
                        auto raw = item.message->encodedContent(KMime::NewlineType::CRLF);
                        receivedBytes += raw.size();
                        if (raw.size() > MimeParser::MaxRawBytes || receivedBytes > MimeParser::MaxRawBytes)
                            transferError = QStringLiteral("受信バッチが安全サイズを超えています。");
                        else {
                            if (!sources.contains(item.uid)) ++receivedDone;
                            sources.insert(item.uid, std::move(raw));
                        }
                    }
                });
                const auto fetchError = runJob(fetch);
                fetch.flushPendingMessages();
                QString result;
                const auto incomplete = !transferError.isEmpty() ? transferError : !fetchError.isEmpty() ? fetchError
                    : sources.size() != pending.size() ? QStringLiteral("同期中にメールが削除されました。再試行してください。") : QString{};
                if (cache) {
                    result = drain(); if (!result.isEmpty()) return result;
                    ingestionCount = sources.size();
                    const auto operation = diagnostics::currentOperation();
                    ingestion = std::async(candidates <= MailPageSize ? std::launch::deferred : std::launch::async,
                        [this, cache, accountId = account.id, folder = selected, validity, operation, syncKey,
                         headers = pending, sources = std::move(sources)]() -> QString {
                            const diagnostics::OperationScope scope(operation);
                            const CancellationScope cancellation(&m_cancelSync);
                            QList<MailRecord> batch;
                            qint64 bytes = 0;
                            for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
                                diagnostics::event("sync-mime", it.value().size());
                                auto parsed = MimeParser::parse(it.value());
                                if (!parsed.ok()) {
                                    const auto saved = batch.isEmpty() ? QString{} : cache->stageMessages(batch, syncKey);
                                    return saved.isEmpty() ? parsed.error : saved;
                                }
                                const auto &header = headers[it.key()];
                                batch.append({accountId, folder, validity, header.uid,
                                    header.flags.contains("\\Seen"), header.flags.contains("\\Flagged"), std::move(parsed.message)});
                                bytes += it.value().size();
                                if (batch.size() >= MailPageSize || bytes >= 8 * 1024 * 1024) {
                                    const auto error = cache->stageMessages(batch, syncKey);
                                    if (!error.isEmpty()) return error;
                                    batch.clear(); bytes = 0;
                                }
                            }
                            return batch.isEmpty() ? QString{} : cache->stageMessages(batch, syncKey);
                        });
                    pending.clear(); pendingBytes = 0;
                    emit syncProgress(account.id, selected, receivedDone, progressTotal);
                    // Drain before reporting a network failure, so complete
                    // messages received before it can be published and reused.
                    if (!incomplete.isEmpty()) { result = drain(); return result.isEmpty() ? incomplete : result; }
                    return {};
                }
                for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
                    if (interrupted()) return QStringLiteral("中断しました。");
                    const auto &raw = it.value(); retainedBytes += raw.size();
                    if (!cache && retainedBytes > 128 * 1024 * 1024) return QStringLiteral("一括取得の安全上限（128 MiB）を超えています。");
                    diagnostics::event("sync-mime", raw.size());
                    const auto parsed = MimeParser::parse(raw); if (!parsed.ok()) return parsed.error;
                    const auto &header = pending[it.key()];
                    MailRecord record{account.id, selected, validity, header.uid, header.flags.contains("\\Seen"), header.flags.contains("\\Flagged"), parsed.message};
                    records.append(record);
                    ++completed;
                }
                emit syncProgress(account.id, selected, receivedDone, progressTotal);
                pending.clear(); pendingBytes = 0;
                return incomplete;
            };
            for (const auto &header : headers) {
                if (interrupted()) return QStringLiteral("中断しました。");
                if (header.size > MimeParser::MaxRawBytes) return QStringLiteral("メールがMIMEサイズ上限を超えているため、このフォルダを同期できません。");
                constexpr qint64 snapshotLimit = 128 * 1024 * 1024;
                if (!cache && header.size > snapshotLimit - retainedBytes) return QStringLiteral("一括取得の安全上限（128 MiB）を超えています。");
                if (reused.contains(header.uid)) { ++completed; ++receivedDone; continue; }
                if (!pending.isEmpty() && (header.size <= 0 || pendingBytes + header.size > 8 * 1024 * 1024)) {
                    jobError = fetchPending(); if (!jobError.isEmpty()) return jobError;
                }
                pending.insert(header.uid, header); pendingBytes += qMax<qint64>(1, header.size);
                if (header.size <= 0 || pendingBytes >= 8 * 1024 * 1024 || pending.size() >= headerWindow) { jobError = fetchPending(); if (!jobError.isEmpty()) return jobError; }
            }
            jobError = fetchPending(); if (!jobError.isEmpty()) return jobError;
            jobError = drain(); if (!jobError.isEmpty()) return jobError;
            if (!boundary.isValid()) { nextWindow = first + headerWindow; fullCompleted = completed; }
            emit syncProgress(account.id, selected, receivedDone, progressTotal);
            }
            jobError = drain(); if (!jobError.isEmpty()) return jobError;
            emit syncProgress(account.id, selected, receivedDone, progressTotal);
            // UID windows survive EXPUNGE and additions. Verify identity, then
            // prune vanished UIDs; a newer UIDNEXT is not a failed refresh.
            KIMAP::SelectJob verify(&session); verify.setMailBox(selected); verify.setOpenReadOnly(true);
            jobError = runJob(verify); if (!jobError.isEmpty()) return jobError;
            if (verify.uidValidity() != validity)
                return QStringLiteral("UIDVALIDITYが変化しました。再度更新してください。");
            if (cache) {
                if (interrupted()) return QStringLiteral("中断しました。");
                KIMAP::SearchJob live(&session); live.setUidBased(true);
                live.setTerm(boundary.isValid()
                    ? KIMAP::Term(KIMAP::Term::SentSince, boundary.date().addDays(-1))
                    : KIMAP::Term(KIMAP::Term::All, QString{}));
                jobError = runJob(live); if (!jobError.isEmpty()) return jobError;
                if (!live.errorText().isEmpty()) return QStringLiteral("UID検索の応答が不正です。");
                auto uids = live.results(); std::sort(uids.begin(), uids.end());
                qint64 previous = 0;
                for (const auto uid : uids) {
                    if (uid <= 0 || uid == previous) return QStringLiteral("UID検索の応答が不正です。");
                    previous = uid;
                }
                uids.removeIf([&](qint64 uid) { return uid >= nextUid; });
                jobError = cache->retainSyncUids(syncKey, uids); if (!jobError.isEmpty()) return jobError;
                jobError = cache->commitSync(publishedFolders(), boundary.isValid() ? -1 : uids.size(), scopedCommit, syncKey);
                if (!jobError.isEmpty()) return jobError;
            }
            return {};
            };
            QString result = (session.state() == KIMAP::Session::Authenticated || session.state() == KIMAP::Session::Selected)
                ? attempt(session) : testImap(account.imap, secret, attempt);
            qint64 lastDownloaded = 0;
            int stalled = 0;
            while (!result.isEmpty() && cache) {
                qint64 downloaded = 0;
                const auto counted = cache->syncDownloadedCount(syncKey, downloaded);
                if (!counted.isEmpty()) return counted;
                if (stageStarted) {
                    const auto saved = cache->commitSync(publishedFolders(), -1, scopedCommit, syncKey, true);
                    if (!saved.isEmpty()) return saved;
                }
                stalled = downloaded > lastDownloaded ? 0 : stalled + 1;
                lastDownloaded = downloaded;
                if (interrupted() || stalled >= 3) return result;
                diagnostics::event("sync-folder-retry", stalled);
                if (session.state() != KIMAP::Session::Disconnected) {
                    // close() queues work on KIMAP's socket thread. Do not open
                    // a replacement until that connection releases its slot.
                    QEventLoop closing;
                    QTimer deadline; deadline.setSingleShot(true);
                    connect(&session, &KIMAP::Session::stateChanged, &closing,
                        [&](KIMAP::Session::State state, KIMAP::Session::State) {
                            if (state == KIMAP::Session::Disconnected) closing.quit();
                        });
                    connect(&deadline, &QTimer::timeout, &closing, &QEventLoop::quit);
                    deadline.start(3000); session.close();
                    if (session.state() != KIMAP::Session::Disconnected) closing.exec();
                    if (session.state() != KIMAP::Session::Disconnected) return result;
                }
                if (interrupted()) return QStringLiteral("中断しました。");
                result = testImap(account.imap, secret, attempt);
            }
            return result;
            };
            // Publish shared LIST metadata once, before any independent folder
            // commits. The primary authenticated connection remains useful.
            jobError = syncFolder(session, targets.first(), singleFolder);
            --m_syncDemand; emit syncCapacityChanged();
            if (!jobError.isEmpty()) return jobError;
            std::mutex queueMutex;
            qsizetype next = 1;
            QString failure;
            const auto take = [&]() -> QString {
                std::lock_guard lock(queueMutex);
                if (!failure.isEmpty() || interrupted() || next >= targets.size()) return {};
                return targets[next++];
            };
            const auto finish = [&](const QString &error) {
                { std::lock_guard lock(queueMutex); if (failure.isEmpty()) failure = error; }
                --m_syncDemand; emit syncCapacityChanged();
            };
            std::atomic_int connections{1};
            std::vector<std::future<void>> helpers;
            const auto operationId = diagnostics::currentOperation();
            const auto startHelpers = [&] {
                while (cache && !singleFolder && connections.load() < qMin(m_syncConcurrency.load(), m_syncDemand.load())) {
                    { std::lock_guard lock(queueMutex); if (!failure.isEmpty() || next >= targets.size()) break; }
                    if (!budget->reserve(true)) break;
                    ++connections;
                    std::future<void> task;
                    try { task = std::async(std::launch::async, [&] {
                        const auto slot = qScopeGuard([&] { --connections; budget->release(); emit syncCapacityChanged(); });
                        const diagnostics::OperationScope operation(operationId);
                        const CancellationScope cancellation(&m_cancelSync);
                        const auto error = testImap(account.imap, secret, [&](KIMAP::Session &helper) -> QString {
                            while (!interrupted()) {
                                if (budget->primaryWaiting() || connections.load() > m_syncConcurrency.load()) break;
                                const auto target = take(); if (target.isEmpty()) break;
                                const auto error = syncFolder(helper, target, true); finish(error);
                                if (!error.isEmpty()) return error;
                            }
                            return {};
                        });
                        if (!error.isEmpty()) { std::lock_guard lock(queueMutex); if (failure.isEmpty()) failure = error; }
                    }); } catch (...) {
                        --connections; budget->release();
                        std::lock_guard lock(queueMutex);
                        failure = QStringLiteral("並列取得ワーカーを開始できません。");
                        break;
                    }
                    helpers.push_back(std::move(task));
                }
            };
            while (!interrupted()) {
                startHelpers();
                const auto target = take(); if (target.isEmpty()) break;
                const auto error = syncFolder(session, target, true); finish(error);
                if (!error.isEmpty()) break;
            }
            for (auto &helper : helpers) {
                try { helper.get(); }
                catch (...) {
                    std::lock_guard lock(queueMutex);
                    if (failure.isEmpty()) failure = QStringLiteral("並列フォルダ取得に失敗しました。");
                }
            }
            if (interrupted()) return QStringLiteral("中断しました。");
            if (!failure.isEmpty()) return failure;
            selected = visibleFolder;
            return {};
        });
        } while (cache && !error.isEmpty() && !folderPhaseStarted && !interrupted() && ++setupFailures < 3);
        }
        if (cache) cache->abortSync(account.id); // Drop only this account's disk-backed staging.
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
                            const bool selectable = isSelectableFolder(flags.value(i));
                            bool drafts = false;
                            for (const auto &flag : flags.value(i)) {
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
