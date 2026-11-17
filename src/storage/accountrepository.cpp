#include "accountrepository.h"
#include "mime/mimeparser.h"
#include "downloadfile.h"
#include "core/diagnostics.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QTimer>
#include <QTimeZone>
#include <QJsonArray>
#include <QJsonObject>
#include <limits>

namespace smoothmail {
namespace {
void recordSqlError(const QSqlError &error)
{
    diagnostics::event("storage-sql-error-type", int(error.type()));
    bool numeric = false; const auto native = error.nativeErrorCode().toLongLong(&numeric);
    diagnostics::event(numeric && native >= 0 ? "storage-sql-native-code" : "storage-sql-native-code-unavailable", numeric ? native : -1);
}
QString sql(QSqlDatabase &db, const QString &statement, const QVariantList &values = {})
{
    QSqlQuery query(db); query.prepare(statement);
    for (const auto &value : values) query.addBindValue(value);
    if (query.exec()) return {};
    recordSqlError(query.lastError()); return query.lastError().text();
}
bool validFolderMetadata(const QList<MailFolder> &folders, diagnostics::FolderWriteSource source)
{
    diagnostics::FolderValidationFailure failure; failure.total = folders.size();
    QHash<QString, qint64> firstIndexes;
    for (qsizetype i = 0; i < folders.size(); ++i) {
        const auto &folder = folders.at(i); const qint64 index = i + 1;
        if (folder.name.isEmpty()) {
            if (!failure.emptyCount++) {
                failure.firstEmptyIndex = index;
                for (const auto &flag : folder.flags) if (flag.toUpper() == "\\NOSELECT") failure.firstEmptyNoSelect = true;
            }
        }
        if (firstIndexes.contains(folder.name)) {
            if (!failure.duplicateCount++) { failure.firstDuplicateIndex = index; failure.duplicateOfIndex = firstIndexes.value(folder.name); }
        } else firstIndexes.insert(folder.name, index);
        if (folder.unread < 0) {
            if (!failure.negativeUnreadCount++) { failure.firstNegativeIndex = index; failure.firstNegativeUnread = folder.unread; }
        }
    }
    if (!failure.emptyCount && !failure.duplicateCount && !failure.negativeUnreadCount) return true;
    diagnostics::folderValidationFailure(source, failure); return false;
}
QJsonArray identifiers(const QList<QByteArray> &values)
{
    QJsonArray result; for (const auto &value : values) result.append(QString::fromLatin1(value)); return result;
}
QVariantList indexValues(const MailRecord &record)
{
    const auto &doc = record.document;
    QJsonArray attachments;
    for (const auto &item : doc.attachments) attachments.append(QJsonObject{{"part", item.partId}, {"name", item.fileName}, {"type", item.mediaType}});
    const QJsonObject meta{{"subject", doc.subject}, {"name", doc.senderName}, {"address", doc.senderAddress},
        {"date", doc.date.isValid() ? QJsonValue(double(doc.date.toMSecsSinceEpoch())) : QJsonValue()}, {"id", QString::fromLatin1(doc.messageId)},
        {"parents", identifiers(doc.inReplyTo)}, {"refs", identifiers(doc.references)},
        {"from", QJsonArray::fromStringList(doc.fromAddresses)}, {"reply", QJsonArray::fromStringList(doc.replyToAddresses)},
        {"hasReply", doc.hasReplyTo}, {"to", QJsonArray::fromStringList(doc.recipients)}, {"attachments", attachments}};
    QString search = doc.subject + u'\n' + doc.senderName + u'\n' + doc.senderAddress + u'\n' + doc.recipients.join(u'\n');
    for (const auto &part : doc.parts) search += u'\n' + part.text;
    return {record.accountId, record.folder, record.uidValidity, record.uid, (record.seen ? 1 : 0) | (record.important ? 2 : 0),
         QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)), search.toCaseFolded()};
}
QString indexMessage(QSqlDatabase &db, const QString &table, const MailRecord &record)
{
    return sql(db, QStringLiteral("INSERT OR REPLACE INTO ") + table + QStringLiteral(" VALUES(?,?,?,?,?,?,?)"), indexValues(record));
}
MailRecord summaryRecord(const QSqlQuery &query, const QString &id, const QString &folder)
{
    const auto meta = QJsonDocument::fromJson(query.value(3).toByteArray()).object();
    MailRecord record{id, folder, query.value(0).toLongLong(), query.value(1).toLongLong(), bool(query.value(2).toInt() & 1), bool(query.value(2).toInt() & 2)};
    auto &doc = record.document;
    doc.subject = meta.value("subject").toString(); doc.senderName = meta.value("name").toString(); doc.senderAddress = meta.value("address").toString();
    if (meta.value("date").isDouble()) doc.date = QDateTime::fromMSecsSinceEpoch(qint64(meta.value("date").toDouble()));
    doc.messageId = meta.value("id").toString().toLatin1();
    for (const auto &item : meta.value("parents").toArray()) doc.inReplyTo.append(item.toString().toLatin1());
    for (const auto &item : meta.value("refs").toArray()) doc.references.append(item.toString().toLatin1());
    for (const auto &item : meta.value("from").toArray()) doc.fromAddresses.append(item.toString());
    for (const auto &item : meta.value("reply").toArray()) doc.replyToAddresses.append(item.toString());
    for (const auto &item : meta.value("to").toArray()) doc.recipients.append(item.toString());
    doc.hasReplyTo = meta.value("hasReply").toBool();
    for (const auto &item : meta.value("attachments").toArray()) {
        const auto attachment = item.toObject(); doc.attachments.append({attachment.value("part").toString(), attachment.value("name").toString(), attachment.value("type").toString()});
    }
    record.storageFolder = query.value(4).toString();
    return record;
}
bool underFolder(const QString &name, const QString &parent, QChar separator)
{
    return name == parent || (!separator.isNull() && name.startsWith(parent + separator));
}
QString journal(QSqlDatabase &db, const QString &id, QList<PendingFolder> &items, qint64 after = 0,
                qint64 through = std::numeric_limits<qint64>::max(), int limit = 10000)
{
    QSqlQuery query(db); query.prepare(QStringLiteral("SELECT revision,operation,source,destination,separator FROM pending_folders WHERE account=? AND revision>? AND revision<=? ORDER BY revision LIMIT ?"));
    for (const auto &value : QVariantList{id, after, through, limit}) query.addBindValue(value);
    if (!query.exec()) return query.lastError().text();
    while (query.next()) { const auto separator = query.value(4).toString(); items.append({query.value(0).toLongLong(), id, query.value(1).toString(), query.value(2).toString(), query.value(3).toString(), separator.isEmpty() ? QChar{} : separator.front()}); }
    return {};
}
void overlayFolders(QList<MailFolder> &folders, const QList<PendingFolder> &operations)
{
    for (const auto &op : operations) {
        if (op.operation == QStringLiteral("create")) {
            bool exists = false; for (const auto &folder : folders) if (folder.name == op.destination) exists = true;
            if (!exists) folders.append({op.destination, op.separator, {}, 0});
        }
        else for (qsizetype i = folders.size(); i-- > 0;) if (underFolder(folders.at(i).name, op.source, op.separator)) {
            if (op.operation == QStringLiteral("delete") || op.operation == QStringLiteral("delete-empty")) folders.removeAt(i);
            else folders[i].name = op.destination + folders.at(i).name.mid(op.source.size());
        }
    }
}
QString serverFolder(QSqlDatabase &db, const QString &id, const QString &local, QString &remote, bool &exists)
{
    QList<PendingFolder> operations; const auto error = journal(db, id, operations); if (!error.isEmpty()) return error;
    remote = local; exists = true;
    for (qsizetype i = operations.size(); i-- > 0;) {
        const auto &op = operations.at(i);
        if (op.operation == QStringLiteral("rename") && underFolder(remote, op.destination, op.separator)) remote = op.source + remote.mid(op.destination.size());
        else if (op.operation == QStringLiteral("create") && underFolder(remote, op.destination, op.separator)) { exists = false; break; }
    }
    return {};
}
QString handleIdentity(QSqlDatabase &db, const QString &handle, const MailRecord &initial, MailRecord &identity)
{
    QSqlQuery query(db); query.prepare(QStringLiteral("SELECT account,folder,uidvalidity,uid FROM message_handles WHERE id=?")); query.addBindValue(handle);
    if (!query.exec()) return query.lastError().text();
    if (query.next()) { identity = {query.value(0).toString(), query.value(1).toString(), query.value(2).toLongLong(), query.value(3).toLongLong()}; identity.storageFolder = identity.folder; return identity.uid > 0 && identity.uidValidity > 0 ? QString{} : QStringLiteral("操作対象のメールは接続先の変更またはフォルダ削除により無効になりました。"); }
    query.finish(); identity = initial;
    QString remote = initial.storageFolder; bool exists = true;
    if (remote.isEmpty()) { const auto error = serverFolder(db, initial.accountId, initial.folder, remote, exists); if (!error.isEmpty()) return error; }
    query.prepare(QStringLiteral("SELECT 1 FROM cache_messages WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"));
    for (const auto &value : QVariantList{initial.accountId, remote, initial.uidValidity, initial.uid}) query.addBindValue(value);
    if (!exists || !query.exec() || !query.next()) return QStringLiteral("操作対象のメールが見つかりません。再取得してください。"); query.finish();
    identity.folder = remote; identity.storageFolder = remote;
    return sql(db, QStringLiteral("INSERT INTO message_handles VALUES(?,?,?,?,?)"), {handle, initial.accountId, remote, initial.uidValidity, initial.uid});
}
}
struct AccountRepository::State
{
    QSqlDatabase db;
    QString error;
    QString connection;
    struct Sync { QString syncId, syncFolder; qint64 syncValidity = 0; diagnostics::OperationId syncOperation = 0; QDateTime boundary; };
    QHash<QString, Sync> syncs;
    QString legacySyncId;
    qint64 flagRevision = 0;

    explicit State(const QString &path)
    {
        connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (path != QStringLiteral(":memory:") && !QDir().mkpath(QFileInfo(path).absolutePath())) {
            error = QStringLiteral("データベースの保存先を作成できません。");
            return;
        }
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
        if (!db.open()) {
            error = db.lastError().text();
            return;
        }
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
            error = query.lastError().text();
            return;
        }
        const auto version = query.value(0).toInt();
        query.finish();
        if (version > 5) {
            error = QStringLiteral("このアプリより新しいデータベース形式です。");
            return;
        }
        if (!db.transaction()) {
            error = db.lastError().text();
            return;
        }
        for (const auto &sql : {
                 QStringLiteral("CREATE TABLE IF NOT EXISTS accounts (id TEXT PRIMARY KEY, position INTEGER NOT NULL, config TEXT NOT NULL)"),
                 QStringLiteral("CREATE TABLE IF NOT EXISTS cache_folders (account TEXT NOT NULL,name TEXT NOT NULL,separator TEXT,flags BLOB,unread INTEGER NOT NULL,PRIMARY KEY(account,name))"),
                  QStringLiteral("CREATE TABLE IF NOT EXISTS cache_messages (account TEXT NOT NULL,folder TEXT NOT NULL,uidvalidity INTEGER NOT NULL,uid INTEGER NOT NULL,flags INTEGER NOT NULL,raw BLOB NOT NULL,PRIMARY KEY(account,folder,uidvalidity,uid))"),
                  QStringLiteral("CREATE TABLE IF NOT EXISTS cache_index (account TEXT NOT NULL,folder TEXT NOT NULL,uidvalidity INTEGER NOT NULL,uid INTEGER NOT NULL,flags INTEGER NOT NULL,meta TEXT NOT NULL,search_text TEXT NOT NULL,PRIMARY KEY(account,folder,uidvalidity,uid))"),
                  QStringLiteral("CREATE INDEX IF NOT EXISTS cache_index_date ON cache_index(account,folder,json_extract(meta,'$.date'),uid)"),
                  QStringLiteral("CREATE INDEX IF NOT EXISTS cache_messages_unread ON cache_messages(account,folder) WHERE (flags & 1)=0"),
                 QStringLiteral("CREATE TABLE IF NOT EXISTS downloads (id TEXT PRIMARY KEY,account TEXT,folder TEXT,uidvalidity INTEGER,uid INTEGER,subject TEXT,path TEXT,size INTEGER,saved_at TEXT)"),
                  QStringLiteral("CREATE TABLE IF NOT EXISTS pending_flags (account TEXT NOT NULL,folder TEXT NOT NULL,uidvalidity INTEGER NOT NULL,uid INTEGER NOT NULL,mask INTEGER NOT NULL,enabled INTEGER NOT NULL,revision INTEGER NOT NULL,PRIMARY KEY(account,folder,uidvalidity,uid,mask))"),
                  QStringLiteral("CREATE TABLE IF NOT EXISTS cache_reset (account TEXT PRIMARY KEY)"),
                  QStringLiteral("CREATE TABLE IF NOT EXISTS pending_folders (revision INTEGER PRIMARY KEY AUTOINCREMENT,account TEXT NOT NULL,operation TEXT NOT NULL,source TEXT NOT NULL,destination TEXT NOT NULL,separator TEXT NOT NULL)"),
                  QStringLiteral("CREATE TABLE IF NOT EXISTS folder_state (account TEXT NOT NULL,folder TEXT NOT NULL,uidvalidity INTEGER NOT NULL,PRIMARY KEY(account,folder))"),
                  QStringLiteral("CREATE TABLE IF NOT EXISTS message_handles (id TEXT PRIMARY KEY,account TEXT NOT NULL,folder TEXT NOT NULL,uidvalidity INTEGER NOT NULL,uid INTEGER NOT NULL)"),
                  QStringLiteral("CREATE TABLE IF NOT EXISTS pending_moves (revision INTEGER PRIMARY KEY AUTOINCREMENT,handle TEXT NOT NULL,account TEXT NOT NULL,source TEXT NOT NULL,uidvalidity INTEGER NOT NULL,uid INTEGER NOT NULL,destination TEXT NOT NULL,attempted INTEGER NOT NULL DEFAULT 0,UNIQUE(account,source,uidvalidity,uid))")}) {
            if (!query.exec(sql)) {
                error = query.lastError().text();
                db.rollback();
                return;
            }
        }
        if (version < 5) {
            bool hasMime = false; if (query.exec(QStringLiteral("PRAGMA table_info(downloads)"))) while (query.next()) if (query.value(1).toString() == QStringLiteral("mime")) hasMime = true; query.finish();
            if ((!hasMime && !query.exec(QStringLiteral("ALTER TABLE downloads ADD COLUMN mime TEXT NOT NULL DEFAULT ''")))
                || !query.exec(QStringLiteral("INSERT OR IGNORE INTO folder_state SELECT account,folder,uidvalidity FROM cache_messages GROUP BY account,folder"))) { error = query.lastError().text(); db.rollback(); return; }
        }
        if (!query.exec(QStringLiteral("PRAGMA user_version=5"))) { error = query.lastError().text(); db.rollback(); return; }
        if (!db.commit()) {
            error = db.lastError().text();
            db.rollback();
        }
        QSqlQuery temporary(db); temporary.exec(QStringLiteral("PRAGMA temp_store=FILE"));
        temporary.exec(QStringLiteral("CREATE TEMP VIEW effective_index AS SELECT c.account,COALESCE(m.destination,c.folder) AS folder,c.uidvalidity,c.uid,c.flags,c.meta,c.search_text,c.folder AS storage_folder FROM cache_index c LEFT JOIN pending_moves m ON m.account=c.account AND m.source=c.folder AND m.uidvalidity=c.uidvalidity AND m.uid=c.uid"));
        if (query.exec(QStringLiteral("SELECT COALESCE(MAX(revision),0) FROM pending_flags")) && query.next()) flagRevision = query.value(0).toLongLong();
    }

    ~State()
    {
        db.close();
        db = {};
        QSqlDatabase::removeDatabase(connection);
    }
};

AccountRepository::AccountRepository(const QString &path, QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    qRegisterMetaType<QList<Account>>();
    qRegisterMetaType<QList<MailFolder>>();
    qRegisterMetaType<QList<MailRecord>>();
    qRegisterMetaType<DownloadEntry>();
    qRegisterMetaType<QList<DownloadEntry>>();
    qRegisterMetaType<MailQuery>(); qRegisterMetaType<MailPage>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [this, path] { m_state = new State(path); }, Qt::QueuedConnection);
    QTimer::singleShot(0, this, &AccountRepository::reload);
}

AccountRepository::~AccountRepository()
{
    cancelPendingReads(); m_thread.requestInterruption();
    QMetaObject::invokeMethod(m_worker, [this] { delete m_state; m_state = nullptr; }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void AccountRepository::reload()
{
    if (m_thread.isInterruptionRequested()) return;
    QMetaObject::invokeMethod(m_worker, [this] {
        // A mutation can enqueue this from the worker while destruction has
        // already queued its state-deletion barrier from the GUI thread.
        if (m_thread.isInterruptionRequested() || !m_state) return;
        if (!m_state->error.isEmpty()) {
            emit failed(QStringLiteral("load"), m_state->error);
            return;
        }
        QSqlQuery query(m_state->db);
        if (!query.exec(QStringLiteral("SELECT config FROM accounts ORDER BY position,id"))) {
            emit failed(QStringLiteral("load"), query.lastError().text());
            return;
        }
        QList<Account> accounts;
        while (query.next()) {
            Account value;
            if (!accountFromJson(QJsonDocument::fromJson(query.value(0).toByteArray()).object(), value)) {
                emit failed(QStringLiteral("load"), QStringLiteral("保存済みアカウント設定が不正です。"));
                return;
            }
            accounts.append(value);
        }
        emit loaded(accounts);
    }, Qt::QueuedConnection);
}

void AccountRepository::insert(const Account &account)
{
    QMetaObject::invokeMethod(m_worker, [this, account] {
        const auto error = validateAccount(account);
        if (!error.isEmpty() || !m_state->error.isEmpty()) {
            emit failed(account.id, error.isEmpty() ? m_state->error : error);
            return;
        }
        QSqlQuery query(m_state->db);
        query.prepare(QStringLiteral("INSERT INTO accounts(id,position,config) VALUES(?,(SELECT COALESCE(MAX(position),-1)+1 FROM accounts),?)"));
        query.addBindValue(account.id);
        query.addBindValue(QJsonDocument(accountJson(account)).toJson(QJsonDocument::Compact));
        if (!query.exec()) {
            emit failed(account.id, query.lastError().text());
            return;
        }
        emit inserted(account.id);
        reload();
    }, Qt::QueuedConnection);
}

void AccountRepository::update(const Account &account)
{
    QMetaObject::invokeMethod(m_worker, [this, account] {
        auto &db = m_state->db;
        const auto fail = [this, account](const QString &error) { emit failed(account.id, error); };
        const auto error = validateAccount(account);
        if (!error.isEmpty() || !m_state->error.isEmpty()) { fail(error.isEmpty() ? m_state->error : error); return; }
        if (!db.transaction()) { fail(db.lastError().text()); return; }
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT config FROM accounts WHERE id=?")); query.addBindValue(account.id);
        if (!query.exec() || !query.next()) { db.rollback(); fail(QStringLiteral("設定が見つかりません。")); return; }
        const auto previous = QJsonDocument::fromJson(query.value(0).toByteArray()).object(); query.finish();
        const bool resetCache = previous.value("imap") != accountJson(account).value("imap");
        query.prepare(QStringLiteral("UPDATE accounts SET config=? WHERE id=?"));
        query.addBindValue(QJsonDocument(accountJson(account)).toJson(QJsonDocument::Compact));
        query.addBindValue(account.id);
        if (!query.exec() || query.numRowsAffected() != 1) { db.rollback(); fail(QStringLiteral("設定を更新できません。")); return; }
        // An active synchronization owns its starting settings. Defer cache
        // identity changes until a synchronization starts with the new settings.
        if (resetCache) {
            const auto error = sql(db, QStringLiteral("INSERT OR IGNORE INTO cache_reset VALUES(?)"), {account.id});
            if (!error.isEmpty()) { db.rollback(); fail(error); return; }
        }
        if (!db.commit()) { db.rollback(); fail(db.lastError().text()); return; }
        emit inserted(account.id); reload();
    }, Qt::QueuedConnection);
}
void AccountRepository::remove(const QString &id)
{
    QMetaObject::invokeMethod(m_worker, [this, id] {
        auto &db = m_state->db;
        const auto fail = [this, id](const QString &error) { emit failed(QStringLiteral("remove:") + id, error); };
        if (!m_state->error.isEmpty()) { fail(m_state->error); return; }
        if (!db.transaction()) { fail(db.lastError().text()); return; }
        QSqlQuery query(db);
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index"), QStringLiteral("cache_folders"), QStringLiteral("pending_flags"), QStringLiteral("pending_folders"), QStringLiteral("folder_state"), QStringLiteral("message_handles"), QStringLiteral("pending_moves"), QStringLiteral("cache_reset"), QStringLiteral("accounts")}) {
            query.prepare(QStringLiteral("DELETE FROM ") + table + (table == QStringLiteral("accounts") ? QStringLiteral(" WHERE id=?") : QStringLiteral(" WHERE account=?")));
            query.addBindValue(id);
            if (!query.exec()) { db.rollback(); fail(query.lastError().text()); return; }
        }
        // Downloaded files and their history are intentionally preserved.
        if (!db.commit()) { db.rollback(); fail(db.lastError().text()); return; }
        emit removed(id); reload();
    }, Qt::QueuedConnection);
}

void AccountRepository::reorder(const QStringList &ids)
{
    QMetaObject::invokeMethod(m_worker, [this, ids] {
        auto &db = m_state->db;
        const auto fail = [this](const QString &error) { emit failed(QStringLiteral("reorder"), error); };
        if (!m_state->error.isEmpty()) {
            fail(m_state->error);
            return;
        }
        QSet<QString> expected;
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("SELECT id FROM accounts"))) {
            fail(query.lastError().text());
            return;
        }
        while (query.next()) expected.insert(query.value(0).toString());
        const QSet<QString> requested(ids.begin(), ids.end());
        if (requested != expected || requested.size() != ids.size()) {
            fail(QStringLiteral("アカウントの並び順が不正です。"));
            return;
        }
        query.finish();
        if (!db.transaction()) {
            fail(db.lastError().text());
            return;
        }
        query.prepare(QStringLiteral("UPDATE accounts SET position=? WHERE id=?"));
        for (qsizetype i = 0; i < ids.size(); ++i) {
            query.bindValue(0, i);
            query.bindValue(1, ids.at(i));
            if (!query.exec()) {
                const auto error = query.lastError().text();
                db.rollback();
                fail(error);
                return;
            }
        }
        if (!db.commit()) {
            const auto error = db.lastError().text();
            db.rollback();
            fail(error);
            return;
        }
        reload();
    }, Qt::QueuedConnection);
}

void AccountRepository::saveSnapshot(const QString &id, const QString &folder, const QList<MailFolder> &folders,
                                     const QList<MailRecord> &messages)
{
    QMetaObject::invokeMethod(m_worker, [this, id, folder, folders, messages] {
        const diagnostics::OperationScope operation(diagnostics::nextOperation());
        diagnostics::event("cache-save-start", messages.size());
        auto &db = m_state->db;
        bool transactionActive = false;
        const auto fail = [this, id, &db, &transactionActive](const QString &error) {
            if (transactionActive) { diagnostics::event(db.rollback() ? "cache-save-rollback" : "cache-save-rollback-failed"); transactionActive = false; }
            diagnostics::event("cache-save-failed");
            emit failed(QStringLiteral("snapshot:") + id, error);
        };
        if (!m_state->error.isEmpty()) { fail(m_state->error); return; }
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT id FROM accounts WHERE id=?"));
        query.addBindValue(id);
        if (!query.exec() || !query.next()) { if (query.lastError().isValid()) recordSqlError(query.lastError()); fail(QStringLiteral("保存先アカウントが見つかりません。")); return; }
        query.finish();
        if (!db.transaction()) { recordSqlError(db.lastError()); fail(db.lastError().text()); return; }
        transactionActive = true;
        const auto exec = [&](const QString &sql, const QVariantList &values) {
            query.prepare(sql);
            for (const auto &value : values) query.addBindValue(value);
            if (query.exec()) return true;
            recordSqlError(query.lastError()); fail(query.lastError().text()); return false;
        };
        if (!validFolderMetadata(folders, diagnostics::FolderWriteSource::Snapshot)) { fail(QStringLiteral("フォルダ情報が不正です。")); return; }
        if (!exec(QStringLiteral("DELETE FROM cache_folders WHERE account=?"), {id})) return;
        QSet<QString> names;
        for (const auto &item : folders) {
            names.insert(item.name);
            QJsonArray flags;
            for (const auto &flag : item.flags) flags.append(QString::fromLatin1(flag));
            if (!exec(QStringLiteral("INSERT INTO cache_folders VALUES(?,?,?,?,?)"),
                {id, item.name, item.separator.isNull() ? QString{} : QString(item.separator), QJsonDocument(flags).toJson(QJsonDocument::Compact), item.unread})) return;
        }
        if (!exec(QStringLiteral("DELETE FROM cache_messages WHERE account=? AND NOT EXISTS (SELECT 1 FROM cache_folders WHERE cache_folders.account=cache_messages.account AND cache_folders.name=cache_messages.folder)"), {id})) return;
        if (!exec(QStringLiteral("DELETE FROM cache_messages WHERE account=? AND folder=?"), {id, folder})) return;
        if (!exec(QStringLiteral("DELETE FROM cache_index WHERE account=? AND (folder=? OR NOT EXISTS (SELECT 1 FROM cache_folders WHERE cache_folders.account=cache_index.account AND cache_folders.name=cache_index.folder))"), {id, folder})) return;
        qint64 validity = 0;
        QSet<qint64> uids;
        for (const auto &message : messages) {
            if (message.accountId != id || message.folder != folder || !names.contains(folder) || message.uid <= 0
                || message.uidValidity <= 0 || (validity && validity != message.uidValidity) || uids.contains(message.uid)
                || message.document.rawSource.isEmpty() || message.document.rawSource.size() > MimeParser::MaxRawBytes) {
                diagnostics::event("cache-save-invalid-message"); fail(QStringLiteral("メッセージのUIDまたはデータが不正です。")); return;
            }
            validity = message.uidValidity;
            uids.insert(message.uid);
            if (!exec(QStringLiteral("INSERT INTO cache_messages VALUES(?,?,?,?,?,?)"),
                {id, folder, validity, message.uid, (message.seen ? 1 : 0) | (message.important ? 2 : 0), message.document.rawSource})) return;
            const auto indexError = indexMessage(db, QStringLiteral("cache_index"), message);
            if (!indexError.isEmpty()) { fail(indexError); return; }
        }
        if (!exec(QStringLiteral("INSERT OR REPLACE INTO folder_state VALUES(?,?,?)"), {id, folder, validity})) return;
        if (!db.commit()) { recordSqlError(db.lastError()); fail(db.lastError().text()); return; }
        transactionActive = false;
        emit snapshotSaved(id, folder, folders, messages);
        diagnostics::event("cache-save-complete", messages.size());
    }, Qt::QueuedConnection);
}

void AccountRepository::loadCache(const QString &id, bool metadataOnly)
{
    QMetaObject::invokeMethod(m_worker, [this, id, metadataOnly] {
        const auto fail = [this, id](const QString &error) { emit failed(QStringLiteral("cache:") + id, error); };
        if (!m_state->error.isEmpty()) { fail(m_state->error); return; }
        QSqlQuery query(m_state->db);
        query.prepare(QStringLiteral("SELECT name,separator,flags,unread FROM cache_folders WHERE account=? ORDER BY CASE WHEN name='INBOX' THEN 0 ELSE 1 END,name"));
        query.addBindValue(id);
        if (!query.exec()) { fail(query.lastError().text()); return; }
        QList<MailFolder> folders;
        while (query.next()) {
            QList<QByteArray> flags;
            const auto flagJson = QJsonDocument::fromJson(query.value(2).toByteArray());
            if (!flagJson.isArray()) { fail(QStringLiteral("保存済みフォルダ情報が不正です。")); return; }
            for (const auto &flag : flagJson.array()) flags.append(flag.toString().toLatin1());
            folders.append({query.value(0).toString(), query.value(1).toString().isEmpty() ? QChar{} : query.value(1).toString().at(0), flags, query.value(3).toLongLong()});
            const auto &folder = folders.last();
            if (folders.size() > 10000 || folder.name.size() > 4096
                || (!folder.separator.isNull() && folder.name.count(folder.separator) > 64)) {
                fail(QStringLiteral("保存済みフォルダの階層または数が安全上限を超えています。")); return;
            }
        }
        if (metadataOnly) {
            QList<PendingFolder> operations; const auto error = journal(m_state->db, id, operations);
            if (!error.isEmpty()) { fail(error); return; } overlayFolders(folders, operations);
            QHash<QString, qint64> unreadChanges;
            query.prepare(QStringLiteral("SELECT m.source,m.destination,COUNT(*) FROM pending_moves m JOIN cache_index c ON c.account=m.account AND c.folder=m.source AND c.uidvalidity=m.uidvalidity AND c.uid=m.uid WHERE m.account=? AND (c.flags & 1)=0 GROUP BY m.source,m.destination")); query.addBindValue(id);
            if (!query.exec()) { fail(query.lastError().text()); return; }
            while (query.next()) { unreadChanges[query.value(0).toString()] -= query.value(2).toLongLong(); unreadChanges[query.value(1).toString()] += query.value(2).toLongLong(); }
            query.finish();
            for (auto &folder : folders) { QString remote; bool exists = true; const auto mappingError = serverFolder(m_state->db, id, folder.name, remote, exists); if (!mappingError.isEmpty()) { fail(mappingError); return; } folder.unread = qMax<qint64>(0, folder.unread + unreadChanges.value(remote)); }
            QString selected;
            for (const auto &folder : folders) {
                bool selectable = true; for (const auto &flag : folder.flags) if (flag.toUpper() == "\\NOSELECT") selectable = false;
                if (selectable) { selected = folder.name; break; }
            }
            emit cacheLoaded(id, selected, folders, {}); return;
        }
        query.prepare(QStringLiteral("SELECT COUNT(*),COALESCE(SUM(length(raw)),0) FROM cache_messages WHERE account=?"));
        query.addBindValue(id);
        if (!query.exec() || !query.next()) { fail(query.lastError().text()); return; }
        if (query.value(0).toLongLong() > 10000 || query.value(1).toLongLong() > 128 * 1024 * 1024) {
            fail(QStringLiteral("保存済みメールが安全上限を超えています。サーバから必要なフォルダを再取得してください。")); return;
        }
        query.finish();
        for (const auto &folder : folders) {
            query.prepare(QStringLiteral("SELECT uidvalidity,uid,flags,raw FROM cache_messages WHERE account=? AND folder=? ORDER BY uid"));
            query.addBindValue(id); query.addBindValue(folder.name);
            if (!query.exec()) { fail(query.lastError().text()); return; }
            QList<MailRecord> messages;
            while (query.next()) {
                const auto parsed = MimeParser::parse(query.value(3).toByteArray());
                if (!parsed.ok()) { fail(parsed.error); return; }
                const int flags = query.value(2).toInt();
                messages.append({id, folder.name, query.value(0).toLongLong(), query.value(1).toLongLong(), bool(flags & 1), bool(flags & 2), parsed.message});
            }
            emit cacheLoaded(id, folder.name, folders, messages);
        }
    }, Qt::QueuedConnection);
}
QString AccountRepository::recentBoundary(const QString &id, const QString &folder, qint64 validity, QDateTime &boundary)
{
    QString error; boundary = {};
    QMetaObject::invokeMethod(m_worker, [&, this] {
        error = m_state->error; if (!error.isEmpty()) return;
        QSqlQuery query(m_state->db);
        // Separate endpoint probes use the date expression index and the
        // identity primary key, instead of decoding every cached JSON row.
        query.prepare(QStringLiteral("SELECT "
            "(SELECT json_extract(meta,'$.date') FROM cache_index WHERE account=? AND folder=? ORDER BY json_extract(meta,'$.date') DESC LIMIT 1),"
            "(SELECT uidvalidity FROM cache_index WHERE account=? AND folder=? ORDER BY uidvalidity LIMIT 1),"
            "(SELECT uidvalidity FROM cache_index WHERE account=? AND folder=? ORDER BY uidvalidity DESC LIMIT 1)"));
        for (int i = 0; i < 3; ++i) { query.addBindValue(id); query.addBindValue(folder); }
        if (!query.exec()) { error = query.lastError().text(); return; }
        if (query.next() && !query.value(0).isNull() && query.value(1).toLongLong() == validity && query.value(2).toLongLong() == validity)
            boundary = QDateTime::fromMSecsSinceEpoch(query.value(0).toLongLong(), QTimeZone::UTC).addSecs(-3600);
    }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::prepareSync(const Account &account)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        auto &db = m_state->db; error = m_state->error; if (!error.isEmpty()) return;
        QSqlQuery query(db); query.prepare(QStringLiteral("SELECT config,EXISTS(SELECT 1 FROM cache_reset WHERE account=accounts.id) FROM accounts WHERE id=?")); query.addBindValue(account.id);
        if (!query.exec() || !query.next()) { error = QStringLiteral("同期先アカウントがありません。"); return; }
        const bool reset = query.value(1).toBool() && QJsonDocument::fromJson(query.value(0).toByteArray()).object().value("imap") == accountJson(account).value("imap"); query.finish();
        if (!reset) return;
        if (!db.transaction()) { error = db.lastError().text(); return; }
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index"), QStringLiteral("cache_folders"), QStringLiteral("pending_flags"), QStringLiteral("pending_folders"), QStringLiteral("folder_state"), QStringLiteral("message_handles"), QStringLiteral("pending_moves"), QStringLiteral("cache_reset")}) {
            error = sql(db, table == QStringLiteral("message_handles") ? QStringLiteral("UPDATE message_handles SET uidvalidity=0,uid=0 WHERE account=?") : QStringLiteral("DELETE FROM ") + table + QStringLiteral(" WHERE account=?"), {account.id});
            if (!error.isEmpty()) break;
        }
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback();
    }, Qt::BlockingQueuedConnection);
    return error;
}
void AccountRepository::queueFolder(const QString &id, const QString &operation, const QString &source, const QString &destination)
{
    QMetaObject::invokeMethod(m_worker, [this, id, operation, source, destination] {
        auto &db = m_state->db;
        const auto fail = [this, id](const QString &error) { emit folderQueued(id, error); };
        if (!m_state->error.isEmpty()) { fail(m_state->error); return; }
        const auto validName = [](const QString &name) { return !name.isEmpty() && name.size() <= 4096 && !name.contains(u'\r') && !name.contains(u'\n') && !name.contains(QChar::Null); };
        const bool deleting = operation == QStringLiteral("delete") || operation == QStringLiteral("delete-empty");
        if ((operation != QStringLiteral("create") && operation != QStringLiteral("rename") && !deleting)
            || (operation != QStringLiteral("create") && !validName(source)) || (!deleting && !validName(destination))) { fail(QStringLiteral("フォルダ操作が不正です。")); return; }
        QSqlQuery query(db); query.prepare(QStringLiteral("SELECT 1 FROM accounts WHERE id=?")); query.addBindValue(id);
        if (!query.exec() || !query.next()) { fail(QStringLiteral("アカウントが見つかりません。")); return; } query.finish();
        QList<MailFolder> folders;
        query.prepare(QStringLiteral("SELECT name,separator,unread FROM cache_folders WHERE account=?")); query.addBindValue(id);
        if (!query.exec()) { fail(query.lastError().text()); return; }
        while (query.next()) { const auto sep = query.value(1).toString(); folders.append({query.value(0).toString(), sep.isEmpty() ? QChar{} : sep.front(), {}, query.value(2).toLongLong()}); } query.finish();
        QList<PendingFolder> operations; auto error = journal(db, id, operations);
        if (!error.isEmpty()) { fail(error); return; }
        if (operations.size() >= 10000) { fail(QStringLiteral("保留中のフォルダ操作が上限に達しています。先に同期してください。")); return; }
        overlayFolders(folders, operations);
        bool foundSource = operation == QStringLiteral("create"), foundDestination = false;
        QChar separator = folders.isEmpty() ? QChar(u'/') : folders.first().separator;
        for (const auto &folder : folders) {
            if (folder.name == source) { foundSource = true; separator = folder.separator; }
            if (folder.name == destination) foundDestination = true;
        }
        if (operation == QStringLiteral("create")) {
            qsizetype longest = -1;
            for (const auto &folder : folders) if (!folder.separator.isNull() && destination.startsWith(folder.name + folder.separator) && folder.name.size() > longest) { longest = folder.name.size(); separator = folder.separator; }
        }
        if (!foundSource || (foundDestination && !deleting)
            || (operation == QStringLiteral("rename") && underFolder(destination, source, separator))
            || (!separator.isNull() && destination.count(separator) > 64)) { fail(QStringLiteral("対象がないか、変更先が既に存在するか、フォルダの階層が不正です。")); return; }
        error = sql(db, QStringLiteral("INSERT INTO pending_folders(account,operation,source,destination,separator) VALUES(?,?,?,?,?)"),
            {id, operation, source.isNull() ? QStringLiteral("") : source, destination.isNull() ? QStringLiteral("") : destination, separator.isNull() ? QStringLiteral("") : QString(separator)});
        emit folderQueued(id, error);
    }, Qt::QueuedConnection);
}
QString AccountRepository::folderRevision(const QString &id, qint64 &revision)
{
    QString error; revision = 0;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        error = m_state->error; if (!error.isEmpty()) return;
        QSqlQuery query(m_state->db); query.prepare(QStringLiteral("SELECT COALESCE(MAX(revision),0) FROM pending_folders WHERE account=?")); query.addBindValue(id);
        if (!query.exec() || !query.next()) error = query.lastError().text(); else revision = query.value(0).toLongLong();
    }, Qt::BlockingQueuedConnection); return error;
}
QString AccountRepository::pendingFolders(const QString &id, qint64 after, qint64 through, QList<PendingFolder> &batch)
{
    QString error; batch.clear();
    QMetaObject::invokeMethod(m_worker, [&, this] { error = m_state->error; if (error.isEmpty()) error = journal(m_state->db, id, batch, after, through, 100); }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::finishFolder(const PendingFolder &op, bool preserveMessages, const QList<MailFolder> &folders)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        auto &db = m_state->db;
        if (!db.transaction()) { error = db.lastError().text(); return; }
        QSqlQuery query(db); query.prepare(QStringLiteral("SELECT 1 FROM accounts WHERE id=?")); query.addBindValue(op.accountId);
        if (!query.exec() || !query.next()) { db.rollback(); error = QStringLiteral("アカウントが削除されました。"); return; } query.finish();
        query.prepare(QStringLiteral("SELECT name FROM cache_folders WHERE account=?")); query.addBindValue(op.accountId);
        if (!query.exec()) { error = query.lastError().text(); db.rollback(); return; }
        QStringList affected;
        while (query.next()) if (op.operation != QStringLiteral("create") && underFolder(query.value(0).toString(), op.source, op.separator)) affected.append(query.value(0).toString()); query.finish();
        for (const auto &name : affected) {
            for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index"), QStringLiteral("pending_flags"), QStringLiteral("message_handles"), QStringLiteral("folder_state")}) {
                if (op.operation == QStringLiteral("rename") && preserveMessages) {
                    const auto target = op.destination + name.mid(op.source.size());
                    error = sql(db, table == QStringLiteral("message_handles") ? QStringLiteral("UPDATE message_handles SET uidvalidity=0,uid=0 WHERE account=? AND folder=?") : QStringLiteral("DELETE FROM ") + table + QStringLiteral(" WHERE account=? AND folder=?"), {op.accountId, target});
                    if (error.isEmpty()) error = sql(db, QStringLiteral("UPDATE ") + table + QStringLiteral(" SET folder=? WHERE account=? AND folder=?"), {target, op.accountId, name});
                } else error = sql(db, table == QStringLiteral("message_handles") ? QStringLiteral("UPDATE message_handles SET uidvalidity=0,uid=0 WHERE account=? AND folder=?") : QStringLiteral("DELETE FROM ") + table + QStringLiteral(" WHERE account=? AND folder=?"), {op.accountId, name});
                if (!error.isEmpty()) break;
            }
            for (const auto &column : {QStringLiteral("source"), QStringLiteral("destination")}) {
                if (!error.isEmpty()) break;
                if (op.operation == QStringLiteral("rename") && preserveMessages) error = sql(db, QStringLiteral("UPDATE pending_moves SET ") + column + QStringLiteral("=? WHERE account=? AND ") + column + QStringLiteral("=?"), {op.destination + name.mid(op.source.size()), op.accountId, name});
                else error = sql(db, QStringLiteral("DELETE FROM pending_moves WHERE account=? AND ") + column + QStringLiteral("=?"), {op.accountId, name});
            }
            if (!error.isEmpty()) break;
        }
        if (error.isEmpty()) error = sql(db, QStringLiteral("DELETE FROM cache_folders WHERE account=?"), {op.accountId});
        for (const auto &folder : folders) {
            if (!error.isEmpty()) break;
            QJsonArray flags; for (const auto &flag : folder.flags) flags.append(QString::fromLatin1(flag));
            error = sql(db, QStringLiteral("INSERT INTO cache_folders VALUES(?,?,?,?,(SELECT COUNT(*) FROM cache_messages WHERE account=? AND folder=? AND (flags & 1)=0))"),
                {op.accountId, folder.name, folder.separator.isNull() ? QStringLiteral("") : QString(folder.separator), QJsonDocument(flags).toJson(QJsonDocument::Compact), op.accountId, folder.name});
        }
        if (error.isEmpty()) error = sql(db, QStringLiteral("DELETE FROM pending_folders WHERE account=? AND revision=?"), {op.accountId, op.revision});
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback();
    }, Qt::BlockingQueuedConnection); return error;
}
QString AccountRepository::beginSync(const QString &id, const QString &folder, qint64 validity, const QDateTime &boundary, quint64 operation)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        State::Sync sync{id, folder.isNull() ? QStringLiteral("") : folder, validity, operation ? operation : diagnostics::nextOperation(), boundary};
        const diagnostics::OperationScope scope(sync.syncOperation);
        diagnostics::event("sync-cache-stage-start");
        auto &db = m_state->db;
        error = m_state->error;
        if (!error.isEmpty()) return;
        for (const auto &statement : {
             QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS sync_messages AS SELECT * FROM cache_messages WHERE 0"),
             QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS temp.sync_uid ON sync_messages(account,uid)"),
             QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS sync_index AS SELECT * FROM cache_index WHERE 0"),
              QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS temp.sync_index_uid ON sync_index(account,uid)"),
              QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS sync_reused (account TEXT NOT NULL,uid INTEGER NOT NULL,flags INTEGER NOT NULL,PRIMARY KEY(account,uid))")}) {
            error = sql(db, statement); if (!error.isEmpty()) return;
        }
        for (const auto &table : {QStringLiteral("sync_messages"), QStringLiteral("sync_index"), QStringLiteral("sync_reused")}) {
            error = sql(db, QStringLiteral("DELETE FROM ") + table + QStringLiteral(" WHERE account=?"), {id});
            if (!error.isEmpty()) return;
        }
        m_state->legacySyncId = id;
        // No selectable mailbox is represented by an empty (not SQL NULL)
        // folder. folder_state.folder is intentionally NOT NULL.
        m_state->syncs.insert(id, sync);
        // Only downloaded bodies are staged. Existing rows remain published
        // until the folder transaction applies deletions and changed flags.
    }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::stageMessages(const QList<MailRecord> &messages, const QString &accountId)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        const auto id = accountId.isEmpty() ? m_state->legacySyncId : accountId;
        if (!m_state->syncs.contains(id)) { error = QStringLiteral("同期セッションがありません。"); return; }
        const auto sync = m_state->syncs.value(id);
        const diagnostics::OperationScope scope(sync.syncOperation);
        auto &db = m_state->db;
        if (messages.size() > MailPageSize || !db.transaction()) { error = QStringLiteral("同期バッチを開始できません。"); return; }
        QSqlQuery body(db), index(db), reused(db);
        if (!body.prepare(QStringLiteral("INSERT OR REPLACE INTO sync_messages VALUES(?,?,?,?,?,?)"))
            || !index.prepare(QStringLiteral("INSERT OR REPLACE INTO sync_index VALUES(?,?,?,?,?,?,?)"))
            || !reused.prepare(QStringLiteral("DELETE FROM sync_reused WHERE account=? AND uid=?"))) {
            error = body.lastError().isValid() ? body.lastError().text()
                : index.lastError().isValid() ? index.lastError().text() : reused.lastError().text();
            db.rollback(); return;
        }
        const auto execute = [&](QSqlQuery &query, const QVariantList &values) {
            for (qsizetype i = 0; i < values.size(); ++i) query.bindValue(int(i), values[i]);
            if (!query.exec()) { error = query.lastError().text(); return false; }
            query.finish(); return true;
        };
        for (const auto &record : messages) {
            if (record.accountId != sync.syncId || record.folder != sync.syncFolder || record.uidValidity != sync.syncValidity
                || record.uid <= 0 || record.document.rawSource.isEmpty() || record.document.rawSource.size() > MimeParser::MaxRawBytes) {
                error = QStringLiteral("同期バッチの識別情報が不正です。"); break;
            }
            if (!execute(body, {record.accountId, record.folder, record.uidValidity, record.uid,
                    (record.seen ? 1 : 0) | (record.important ? 2 : 0), record.document.rawSource})
                || !execute(index, indexValues(record)) || !execute(reused, {sync.syncId, record.uid})) break;
        }
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback();
    }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::reuseMessage(qint64 uid, bool seen, bool important, bool &found, const QString &accountId)
{
    QSet<qint64> reused;
    const auto error = reuseMessages({{uid, seen, important}}, reused, accountId);
    found = reused.contains(uid);
    return error;
}
QString AccountRepository::reuseMessages(const QList<SyncFlags> &messages, QSet<qint64> &found, const QString &accountId)
{
    QString error; found.clear();
    QMetaObject::invokeMethod(m_worker, [&, this] {
        const auto id = accountId.isEmpty() ? m_state->legacySyncId : accountId;
        if (!m_state->syncs.contains(id)) { error = QStringLiteral("同期セッションがありません。"); return; }
        const auto sync = m_state->syncs.value(id);
        const diagnostics::OperationScope scope(sync.syncOperation);
        auto &db = m_state->db;
        if (messages.size() > MailPageSize) { error = QStringLiteral("再利用バッチが上限を超えています。"); return; }
        QSet<qint64> unique;
        for (const auto &message : messages) {
            if (message.uid <= 0 || unique.contains(message.uid)) { error = QStringLiteral("再利用バッチのUIDが不正です。"); return; }
            unique.insert(message.uid);
        }
        if (!db.transaction()) { error = db.lastError().text(); return; }
        QSqlQuery query(db), insert(db);
        query.prepare(QStringLiteral("SELECT json_extract(meta,'$.date') FROM cache_index i WHERE account=? AND folder=? AND uidvalidity=? AND uid=? AND EXISTS (SELECT 1 FROM cache_messages m WHERE m.account=i.account AND m.folder=i.folder AND m.uidvalidity=i.uidvalidity AND m.uid=i.uid)"));
        insert.prepare(QStringLiteral("INSERT OR REPLACE INTO sync_reused VALUES(?,?,?)"));
        for (const auto &message : messages) {
            query.bindValue(0, sync.syncId); query.bindValue(1, sync.syncFolder); query.bindValue(2, sync.syncValidity); query.bindValue(3, message.uid);
            if (!query.exec()) { error = query.lastError().text(); break; }
            if (!query.next()) { query.finish(); continue; }
            const auto date = query.value(0); query.finish();
            // Cached MIME/Date is immutable within a UIDVALIDITY. A search may
            // over-select the preceding day; don't refresh those older flags.
            if (!sync.boundary.isValid() || date.isNull() || date.toLongLong() >= sync.boundary.toMSecsSinceEpoch()) {
                insert.bindValue(0, id); insert.bindValue(1, message.uid); insert.bindValue(2, (message.seen ? 1 : 0) | (message.important ? 2 : 0));
                if (!insert.exec()) { error = insert.lastError().text(); break; }
                insert.finish();
            }
            found.insert(message.uid);
        }
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) { db.rollback(); found.clear(); }
    }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::resolveSyncFolder(const QString &id, const QString &local, QString &remote, bool &exists)
{
    QString error; QMetaObject::invokeMethod(m_worker, [&, this] { error = m_state->error; if (error.isEmpty()) error = serverFolder(m_state->db, id, local, remote, exists); }, Qt::BlockingQueuedConnection); return error;
}
QString AccountRepository::commitSync(const QList<MailFolder> &folders, qint64 expectedCount, bool folderOnly, const QString &accountId)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        const auto id = accountId.isEmpty() ? m_state->legacySyncId : accountId;
        if (!m_state->syncs.contains(id)) { error = QStringLiteral("同期セッションがありません。"); return; }
        const auto sync = m_state->syncs.value(id);
        const auto folder = sync.syncFolder;
        const diagnostics::OperationScope scope(sync.syncOperation);
        diagnostics::event("sync-cache-save-start", folders.size());
        auto &db = m_state->db;
        bool transactionActive = false;
        const auto fail = [&](const QString &message) {
            error = message;
            if (transactionActive) { diagnostics::event(db.rollback() ? "sync-cache-rollback" : "sync-cache-rollback-failed"); transactionActive = false; }
            diagnostics::event("sync-cache-failed");
        };
        QSqlQuery count(db);
        count.prepare(QStringLiteral("SELECT (SELECT COUNT(*) FROM sync_messages WHERE account=?) + (SELECT COUNT(*) FROM sync_reused r WHERE account=? AND NOT EXISTS (SELECT 1 FROM sync_messages m WHERE m.account=r.account AND m.uid=r.uid))"));
        count.addBindValue(id); count.addBindValue(id);
        const bool countAvailable = count.exec() && count.next();
        if (!countAvailable || (expectedCount >= 0 && count.value(0).toLongLong() != expectedCount)) {
            if (count.lastError().isValid()) recordSqlError(count.lastError());
            diagnostics::event(countAvailable ? "sync-cache-count-mismatch" : "sync-cache-count-query-failed");
            diagnostics::event("sync-cache-expected-count", expectedCount);
            if (countAvailable) diagnostics::event("sync-cache-actual-count", count.value(0).toLongLong());
            fail(QStringLiteral("同期中にメール件数が変化しました。再試行してください。")); return;
        }
        count.finish();
        if (!db.transaction()) { recordSqlError(db.lastError()); fail(db.lastError().text()); return; }
        transactionActive = true;
        const auto run = [&](const QString &statement, const QVariantList &values = {}) {
            error = sql(db, statement, values); return error.isEmpty();
        };
        QSqlQuery account(db); account.prepare(QStringLiteral("SELECT 1 FROM accounts WHERE id=?")); account.addBindValue(id);
        if (!account.exec() || !account.next()) { if (account.lastError().isValid()) recordSqlError(account.lastError()); diagnostics::event("sync-cache-account-unavailable"); fail(QStringLiteral("同期先アカウントがありません。")); return; } account.finish();
        if (folderOnly) {
            bool present = false; for (const auto &item : folders) if (item.name == folder) present = true;
            if (!present) { diagnostics::event("sync-cache-target-unavailable"); fail(QStringLiteral("更新対象のフォルダ情報がありません。")); return; }
        }
        if (!validFolderMetadata(folders, diagnostics::FolderWriteSource::SyncCommit)) { fail(QStringLiteral("フォルダ情報が不正です。")); return; }
        if (!folderOnly) {
            QSet<QString> listed; for (const auto &item : folders) listed.insert(item.name);
            QSqlQuery cached(db); cached.prepare(QStringLiteral("SELECT name FROM cache_folders WHERE account=?")); cached.addBindValue(id);
            if (!cached.exec()) { fail(cached.lastError().text()); return; }
            QStringList removed;
            while (cached.next()) if (!listed.contains(cached.value(0).toString())) removed.append(cached.value(0).toString());
            if (cached.lastError().isValid()) { fail(cached.lastError().text()); return; }
            cached.finish();
            for (const auto &name : removed) if (!run(QStringLiteral("DELETE FROM cache_folders WHERE account=? AND name=?"), {id, name})) { fail(error); return; }
        }
        for (const auto &item : folders) {
            if (folderOnly && item.name != folder) continue;
            QJsonArray flags; for (const auto &flag : item.flags) flags.append(QString::fromLatin1(flag));
            if (!run(QStringLiteral("INSERT INTO cache_folders VALUES(?,?,?,?,?) ON CONFLICT(account,name) DO UPDATE SET separator=excluded.separator,flags=excluded.flags,unread=excluded.unread WHERE cache_folders.separator IS NOT excluded.separator OR cache_folders.flags IS NOT excluded.flags OR cache_folders.unread<>excluded.unread"), {id, item.name, item.separator.isNull() ? QString{} : QString(item.separator), QJsonDocument(flags).toJson(QJsonDocument::Compact), item.unread})) break;
        }
        if (!error.isEmpty()) { fail(error); return; }
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index")}) {
            if (!folderOnly && !run(QStringLiteral("DELETE FROM ") + table + QStringLiteral(" AS c WHERE account=? AND NOT EXISTS (SELECT 1 FROM cache_folders f WHERE f.account=c.account AND f.name=c.folder)"), {id})) break;
            if (!run(QStringLiteral("DELETE FROM ") + table + QStringLiteral(" WHERE account=? AND folder=? AND uidvalidity<>?"), {id, folder, sync.syncValidity})) break;
            // Delete only missing rows in this refresh window. Old dates remain
            // untouched in Recent mode; a full sync considers every cached UID.
            QString window;
            QVariantList values{id, folder, sync.syncValidity};
            if (sync.boundary.isValid()) {
                window = table == QStringLiteral("cache_index") ? QStringLiteral(" AND json_extract(meta,'$.date')>=?")
                    : QStringLiteral(" AND uid IN (SELECT uid FROM cache_index WHERE account=? AND folder=? AND uidvalidity=? AND json_extract(meta,'$.date')>=?)");
                if (table == QStringLiteral("cache_messages")) values.append(QVariantList{id, folder, sync.syncValidity});
                values.append(sync.boundary.toMSecsSinceEpoch());
            }
            if (!run(QStringLiteral("DELETE FROM ") + table + QStringLiteral(" AS c WHERE account=? AND folder=? AND uidvalidity=?") + window
                + QStringLiteral(" AND NOT EXISTS (SELECT 1 FROM sync_reused r WHERE r.account=c.account AND r.uid=c.uid) AND NOT EXISTS (SELECT 1 FROM sync_messages m WHERE m.account=c.account AND m.uid=c.uid)"), values)) break;
        }
        if (error.isEmpty()) run(QStringLiteral("INSERT OR REPLACE INTO cache_messages SELECT * FROM sync_messages WHERE account=?"), {id});
        if (error.isEmpty()) run(QStringLiteral("INSERT OR REPLACE INTO cache_index SELECT * FROM sync_index WHERE account=?"), {id});
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index")}) {
            if (error.isEmpty()) run(QStringLiteral("UPDATE ") + table + QStringLiteral(" AS c SET flags=(SELECT flags FROM sync_reused r WHERE r.account=c.account AND r.uid=c.uid) WHERE account=? AND folder=? AND uidvalidity=? AND uid IN (SELECT uid FROM sync_reused WHERE account=?) AND flags<>(SELECT flags FROM sync_reused r WHERE r.account=c.account AND r.uid=c.uid)"), {id, folder, sync.syncValidity, id});
        }
        if (error.isEmpty()) run(QStringLiteral("INSERT INTO folder_state VALUES(?,?,?) ON CONFLICT(account,folder) DO UPDATE SET uidvalidity=excluded.uidvalidity WHERE folder_state.uidvalidity<>excluded.uidvalidity"), {id, folder, sync.syncValidity});
        // Edits made while the network worker was fetching must win locally
        // until their own revision is submitted on a later synchronization.
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index")}) {
            if (error.isEmpty()) run(QStringLiteral("UPDATE ") + table + QStringLiteral(" AS c SET flags=(flags & ~COALESCE((SELECT SUM(mask) FROM pending_flags p WHERE p.account=c.account AND p.folder=c.folder AND p.uidvalidity=c.uidvalidity AND p.uid=c.uid),0)) | COALESCE((SELECT SUM(mask*enabled) FROM pending_flags p WHERE p.account=c.account AND p.folder=c.folder AND p.uidvalidity=c.uidvalidity AND p.uid=c.uid),0) WHERE account=? AND folder=? AND uid IN (SELECT uid FROM pending_flags WHERE account=? AND folder=?)"), {id, folder, id, folder});
        }
        if (error.isEmpty()) {
            const auto prune = folderOnly
                ? QStringLiteral("DELETE FROM pending_flags AS p WHERE account=? AND folder=? AND NOT EXISTS (SELECT 1 FROM cache_messages c WHERE c.account=p.account AND c.folder=p.folder AND c.uidvalidity=p.uidvalidity AND c.uid=p.uid)")
                : QStringLiteral("DELETE FROM pending_flags AS p WHERE account=? AND (NOT EXISTS (SELECT 1 FROM cache_folders f WHERE f.account=p.account AND f.name=p.folder) OR (folder=? AND NOT EXISTS (SELECT 1 FROM cache_messages c WHERE c.account=p.account AND c.folder=p.folder AND c.uidvalidity=p.uidvalidity AND c.uid=p.uid)))");
            run(prune, {id, folder});
        }
        if (error.isEmpty()) run(QStringLiteral("UPDATE cache_folders AS f SET unread=(SELECT COUNT(*) FROM cache_messages c WHERE c.account=f.account AND c.folder=f.name AND (c.flags & 1)=0) WHERE account=? AND ")
            + (folderOnly ? QStringLiteral("name=?") : QStringLiteral("(name=? OR EXISTS (SELECT 1 FROM cache_messages c WHERE c.account=f.account AND c.folder=f.name))"))
            + QStringLiteral(" AND unread<>(SELECT COUNT(*) FROM cache_messages c WHERE c.account=f.account AND c.folder=f.name AND (c.flags & 1)=0)"), {id, folder});
        if (error.isEmpty() && !db.commit()) { recordSqlError(db.lastError()); error = db.lastError().text(); }
        if (!error.isEmpty()) { fail(error); return; }
        transactionActive = false; diagnostics::event("sync-cache-commit", expectedCount);
    }, Qt::BlockingQueuedConnection);
    return error;
}
void AccountRepository::abortSync(const QString &accountId)
{
    QMetaObject::invokeMethod(m_worker, [this, accountId] {
        const auto id = accountId.isEmpty() ? m_state->legacySyncId : accountId;
        if (!m_state->syncs.contains(id)) return;
        const diagnostics::OperationScope scope(m_state->syncs.value(id).syncOperation);
        sql(m_state->db, QStringLiteral("DELETE FROM sync_messages WHERE account=?"), {id});
        sql(m_state->db, QStringLiteral("DELETE FROM sync_index WHERE account=?"), {id});
        sql(m_state->db, QStringLiteral("DELETE FROM sync_reused WHERE account=?"), {id});
        m_state->syncs.remove(id);
        if (m_state->legacySyncId == id) m_state->legacySyncId.clear();
    }, Qt::BlockingQueuedConnection);
}

void AccountRepository::loadPage(const MailQuery &request)
{
    const auto serial = ++m_pageRequest;
    QMetaObject::invokeMethod(m_worker, [this, request, serial] {
        if (serial != m_pageRequest.load()) return;
        MailPage page; page.query = request;
        auto &db = m_state->db;
        const auto deliverError = [&](const QString &error) { page.error = error; emit pageLoaded(page); };
        if (request.pageSize < 1 || request.pageSize > 1000) { deliverError(QStringLiteral("1ページの件数が範囲外です。")); return; }
        if (!m_state->error.isEmpty()) { deliverError(m_state->error); return; }
        QString remote; bool exists = false;
        const auto pathError = serverFolder(db, request.accountId, request.folder, remote, exists);
        if (!pathError.isEmpty()) { deliverError(pathError); return; }
        // Upgrade v1 caches one message at a time, without ever loading a folder.
        QSqlQuery missing(db);
        missing.prepare(QStringLiteral("SELECT c.uidvalidity,c.uid,c.flags,c.raw FROM cache_messages c WHERE c.account=? AND c.folder=? AND NOT EXISTS (SELECT 1 FROM cache_index i WHERE i.account=c.account AND i.folder=c.folder AND i.uidvalidity=c.uidvalidity AND i.uid=c.uid)"));
        missing.addBindValue(request.accountId); missing.addBindValue(remote);
        if (!missing.exec()) { deliverError(missing.lastError().text()); return; }
        while (missing.next()) {
            if (serial != m_pageRequest.load() || m_thread.isInterruptionRequested()) return;
            const auto parsed = MimeParser::parse(missing.value(3).toByteArray());
            if (!parsed.ok()) { deliverError(parsed.error); return; }
            const int flags = missing.value(2).toInt();
            const MailRecord record{request.accountId, remote, missing.value(0).toLongLong(), missing.value(1).toLongLong(), bool(flags & 1), bool(flags & 2), parsed.message};
            const auto error = indexMessage(db, QStringLiteral("cache_index"), record);
            if (!error.isEmpty()) { deliverError(error); return; }
        }
        missing.finish();
        QString where = QStringLiteral(" WHERE account=? AND folder=?");
        QVariantList values{request.accountId, remote};
        if (!request.text.isEmpty()) { where += QStringLiteral(" AND instr(search_text,?)>0"); values.append(request.text.toCaseFolded()); }
        if (request.importantOnly) where += QStringLiteral(" AND (flags & 2)!=0");
        if (request.unreadOnly) where += QStringLiteral(" AND (flags & 1)=0");
        const QStringList columns{QStringLiteral("json_extract(meta,'$.subject') COLLATE NOCASE"), QStringLiteral("json_extract(meta,'$.name') COLLATE NOCASE"),
            QStringLiteral("json_extract(meta,'$.address') COLLATE NOCASE"), QStringLiteral("json_extract(meta,'$.date')")};
        const auto order = QStringLiteral(" ORDER BY ") + columns.value(request.sortColumn - 3, columns.last())
            + (request.order == Qt::AscendingOrder ? QStringLiteral(" ASC,uid ASC,storage_folder ASC") : QStringLiteral(" DESC,uid DESC,storage_folder DESC"));
        const auto execute = [&](QSqlQuery &query, const QString &statement, const QVariantList &bindings) {
            query.prepare(statement); for (const auto &value : bindings) query.addBindValue(value); return query.exec();
        };
        QSqlQuery query(db);
        if (!execute(query, QStringLiteral("SELECT COUNT(*) FROM effective_index") + where, values) || !query.next()) { deliverError(query.lastError().text()); return; }
        page.total = query.value(0).toLongLong(); query.finish();
        if (request.revealUid > 0) {
            auto bindings = values; bindings.append(request.revealUid); bindings.append(request.revealValidity);
            if (!request.revealStorageFolder.isEmpty()) bindings.append(request.revealStorageFolder);
            if (!execute(query, QStringLiteral("SELECT position FROM (SELECT uid,uidvalidity,storage_folder,ROW_NUMBER() OVER(") + order + QStringLiteral(") AS position FROM effective_index")
                + where + QStringLiteral(") WHERE uid=? AND uidvalidity=?")
                + (request.revealStorageFolder.isEmpty() ? QString{} : QStringLiteral(" AND storage_folder=?")), bindings)) { deliverError(query.lastError().text()); return; }
            if (query.next()) page.query.page = int((query.value(0).toLongLong() - 1) / request.pageSize + 1);
            query.finish();
        }
        page.query.page = qBound(1, page.query.page, int(qMax<qint64>(1, (page.total + request.pageSize - 1) / request.pageSize)));
        auto bindings = values; bindings.append(request.pageSize); bindings.append(qint64(page.query.page - 1) * request.pageSize);
        if (!execute(query, QStringLiteral("SELECT uidvalidity,uid,flags,meta,storage_folder FROM effective_index") + where + order + QStringLiteral(" LIMIT ? OFFSET ?"), bindings)) { deliverError(query.lastError().text()); return; }
        while (query.next()) page.messages.append(summaryRecord(query, request.accountId, request.folder));
        if (serial == m_pageRequest.load()) emit pageLoaded(page);
    }, Qt::QueuedConnection);
}
void AccountRepository::ensureFolderLoaded(const QString &id, const QString &folder)
{
    QMetaObject::invokeMethod(m_worker, [this, id, folder] {
        if (!m_state->error.isEmpty()) return;
        QString remote; bool exists = true; const auto error = serverFolder(m_state->db, id, folder, remote, exists); if (!error.isEmpty() || !exists) return;
        QSqlQuery query(m_state->db); query.prepare(folder.isEmpty() ? QStringLiteral("SELECT 1 FROM folder_state WHERE account=? LIMIT 1") : QStringLiteral("SELECT 1 FROM folder_state WHERE account=? AND folder=?"));
        query.addBindValue(id); if (!folder.isEmpty()) query.addBindValue(remote);
        if (query.exec() && !query.next()) emit initialFetchRequired(id, folder);
    }, Qt::QueuedConnection);
}
void AccountRepository::setFlagIntent(const QString &handle, const MailRecord &initial, bool important, bool enabled)
{
    QMetaObject::invokeMethod(m_worker, [this, handle, initial, important, enabled] {
        MailRecord identity; const auto error = handleIdentity(m_state->db, handle, initial, identity);
        if (!error.isEmpty()) { emit localOperationFinished(initial.accountId, error); return; }
        queueFlag(identity, important, enabled);
    }, Qt::QueuedConnection);
}
void AccountRepository::setMoveIntent(const QString &handle, const MailRecord &initial, const QString &destination)
{
    QMetaObject::invokeMethod(m_worker, [this, handle, initial, destination] {
        auto &db = m_state->db; MailRecord identity; auto error = handleIdentity(db, handle, initial, identity);
        QString target; bool exists = true;
        if (error.isEmpty()) {
            QList<MailFolder> folders; QSqlQuery query(db); query.prepare(QStringLiteral("SELECT name,flags,separator FROM cache_folders WHERE account=?")); query.addBindValue(initial.accountId);
            if (!query.exec()) error = query.lastError().text();
            while (query.next()) { QList<QByteArray> flags; for (const auto &flag : QJsonDocument::fromJson(query.value(1).toByteArray()).array()) flags.append(flag.toString().toLatin1()); const auto separator = query.value(2).toString(); folders.append({query.value(0).toString(), separator.isEmpty() ? QChar{} : separator.front(), flags, 0}); }
            query.finish(); QList<PendingFolder> operations;
            if (error.isEmpty()) error = journal(db, initial.accountId, operations);
            overlayFolders(folders, operations);
            bool found = false;
            for (const auto &folder : folders) if (folder.name == destination) { found = true; for (const auto &flag : folder.flags) if (flag.toUpper() == "\\NOSELECT") found = false; break; }
            if (error.isEmpty() && !found) error = QStringLiteral("移動先フォルダが存在しないか選択できません。");
        }
        if (error.isEmpty()) error = serverFolder(db, initial.accountId, destination, target, exists);
        if (error.isEmpty() && (target.isEmpty() || target.contains(u'\r') || target.contains(u'\n') || target.contains(QChar::Null))) error = QStringLiteral("移動先フォルダが不正です。");
        if (!error.isEmpty()) { emit localOperationFinished(initial.accountId, error); return; }
        const QVariantList key{identity.accountId, identity.folder, identity.uidValidity, identity.uid};
        QSqlQuery prior(db); prior.prepare(QStringLiteral("SELECT attempted FROM pending_moves WHERE account=? AND source=? AND uidvalidity=? AND uid=?")); for (const auto &value : key) prior.addBindValue(value);
        const bool active = prior.exec() && prior.next() && prior.value(0).toBool(); prior.finish();
        if (target == identity.folder && !active) error = sql(db, QStringLiteral("DELETE FROM pending_moves WHERE account=? AND source=? AND uidvalidity=? AND uid=?"), key);
        else error = sql(db, QStringLiteral("INSERT OR REPLACE INTO pending_moves(handle,account,source,uidvalidity,uid,destination,attempted) VALUES(?,?,?,?,?,?,?)"), {handle, identity.accountId, identity.folder, identity.uidValidity, identity.uid, target, active ? 1 : 0});
        emit localOperationFinished(initial.accountId, error);
    }, Qt::QueuedConnection);
}
QString AccountRepository::moveRevision(const QString &id, qint64 &revision)
{
    QString error; revision = 0;
    QMetaObject::invokeMethod(m_worker, [&, this] { QSqlQuery query(m_state->db); query.prepare(QStringLiteral("SELECT COALESCE(MAX(revision),0) FROM pending_moves WHERE account=?")); query.addBindValue(id); if (!query.exec() || !query.next()) error = query.lastError().text(); else revision = query.value(0).toLongLong(); }, Qt::BlockingQueuedConnection); return error;
}
QString AccountRepository::pendingMoves(const QString &id, qint64 after, qint64 through, QList<PendingMove> &moves)
{
    QString error; moves.clear();
    QMetaObject::invokeMethod(m_worker, [&, this] {
        QSqlQuery query(m_state->db); query.prepare(QStringLiteral("SELECT revision,handle,source,uidvalidity,uid,destination,attempted FROM pending_moves WHERE account=? AND revision>? AND revision<=? ORDER BY revision LIMIT 100"));
        for (const auto &value : QVariantList{id, after, through}) query.addBindValue(value);
        if (!query.exec()) { error = query.lastError().text(); return; }
        while (query.next()) moves.append({query.value(0).toLongLong(), query.value(1).toString(), {id, query.value(2).toString(), query.value(3).toLongLong(), query.value(4).toLongLong()}, query.value(5).toString(), query.value(6).toBool()});
    }, Qt::BlockingQueuedConnection); return error;
}
QString AccountRepository::markMove(qint64 revision)
{
    QString error; QMetaObject::invokeMethod(m_worker, [&, this] {
        QSqlQuery query(m_state->db); query.prepare(QStringLiteral("UPDATE pending_moves SET attempted=1 WHERE revision=?")); query.addBindValue(revision);
        if (!query.exec()) error = query.lastError().text();
        else if (query.numRowsAffected() != 1) error = QStringLiteral("移動操作が変更されました。次回取得時に新しい操作を反映します。");
    }, Qt::BlockingQueuedConnection); return error;
}
QString AccountRepository::abandonMove(qint64 revision)
{
    QString error; QMetaObject::invokeMethod(m_worker, [&, this] { error = sql(m_state->db, QStringLiteral("DELETE FROM pending_moves WHERE revision=?"), {revision}); }, Qt::BlockingQueuedConnection); return error;
}
QString AccountRepository::finishMove(const PendingMove &move, qint64 validity, qint64 uid)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        auto &db = m_state->db; const auto &identity = move.identity;
        if (validity <= 0 || uid <= 0 || !db.transaction()) { error = QStringLiteral("移動後のUIDを確認できません。"); return; }
        const QVariantList oldKey{identity.accountId, identity.folder, identity.uidValidity, identity.uid};
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index"), QStringLiteral("pending_flags")}) {
            error = sql(db, QStringLiteral("DELETE FROM ") + table + QStringLiteral(" WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"), {identity.accountId, move.destination, validity, uid});
            QVariantList values{move.destination, validity, uid}; values.append(oldKey);
            if (error.isEmpty()) error = sql(db, QStringLiteral("UPDATE ") + table + QStringLiteral(" SET folder=?,uidvalidity=?,uid=? WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"), values);
            if (!error.isEmpty()) break;
        }
        QVariantList values{move.destination, validity, uid}; values.append(oldKey);
        if (error.isEmpty()) error = sql(db, QStringLiteral("UPDATE message_handles SET folder=?,uidvalidity=?,uid=? WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"), values);
        if (error.isEmpty()) error = sql(db, QStringLiteral("UPDATE downloads SET folder=?,uidvalidity=?,uid=? WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"), values);
        if (error.isEmpty()) error = sql(db, QStringLiteral("DELETE FROM pending_moves WHERE revision=?"), {move.revision});
        if (error.isEmpty()) error = sql(db, QStringLiteral("UPDATE pending_moves SET source=?,uidvalidity=?,uid=? WHERE account=? AND source=? AND uidvalidity=? AND uid=?"), values);
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback();
    }, Qt::BlockingQueuedConnection); return error;
}
void AccountRepository::cancelCreatedFolder(const QString &id, const QString &name)
{
    QMetaObject::invokeMethod(m_worker, [this, id, name] {
        auto &db = m_state->db; QString origin; bool exists = true; auto error = serverFolder(db, id, name, origin, exists);
        if (!error.isEmpty()) { emit localOperationFinished(id, error); return; }
        QSqlQuery query(db); query.prepare(QStringLiteral("SELECT 1 FROM effective_index WHERE account=? AND folder=? LIMIT 1")); query.addBindValue(id); query.addBindValue(origin);
        if (!query.exec() || query.next()) { emit localOperationFinished(id, QStringLiteral("空でないフォルダの作成は元に戻せません。")); return; }
        query.finish();
        // Append the inverse even before CREATE is sent. Removing journal rows races
        // a worker that has already captured them and can lose the undo entirely.
        queueFolder(id, QStringLiteral("delete-empty"), name, {});
    }, Qt::QueuedConnection);
}
void AccountRepository::restoreCreatedFolder(const QString &id, const QString &name)
{
    queueFolder(id, QStringLiteral("create"), {}, name);
}
void AccountRepository::loadMessage(const MailRecord &identity)
{
    const auto serial = ++m_bodyRequest;
    QMetaObject::invokeMethod(m_worker, [this, identity, serial] {
        if (serial != m_bodyRequest.load()) return;
        QString remote; bool exists = false;
        const auto pathError = serverFolder(m_state->db, identity.accountId, identity.folder, remote, exists);
        if (!identity.storageFolder.isEmpty()) { remote = identity.storageFolder; exists = true; }
        if (!pathError.isEmpty() || !exists) { emit messageLoaded(identity, pathError.isEmpty() ? QStringLiteral("このフォルダはまだサーバに作成されていません。") : pathError); return; }
        QSqlQuery query(m_state->db);
        query.prepare(QStringLiteral("SELECT raw,flags FROM cache_messages WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"));
        for (const auto &value : QVariantList{identity.accountId, remote, identity.uidValidity, identity.uid}) query.addBindValue(value);
        if (!query.exec() || !query.next()) { emit messageLoaded(identity, QStringLiteral("メッセージが見つかりません。")); return; }
        auto record = identity; const auto parsed = MimeParser::parse(query.value(0).toByteArray());
        record.document = parsed.message; record.seen = query.value(1).toInt() & 1; record.important = query.value(1).toInt() & 2;
        if (serial == m_bodyRequest.load()) emit messageLoaded(record, parsed.error);
    }, Qt::QueuedConnection);
}
void AccountRepository::trackMessage(const QString &handle, const MailRecord &initial)
{
    QMetaObject::invokeMethod(m_worker, [this, handle, initial] {
        if (!m_state->error.isEmpty() || handle.isEmpty()) return;
        MailRecord identity;
        // Register before subsequent moves/renames. A failed registration is
        // reported as a missing source on lookup; never rebind from old UIDs.
        handleIdentity(m_state->db, handle, initial, identity);
    }, Qt::QueuedConnection);
}
void AccountRepository::locateMessage(const QString &handle)
{
    QMetaObject::invokeMethod(m_worker, [this, handle] {
        MailRecord identity; QString error = m_state->error;
        if (error.isEmpty()) {
            QSqlQuery query(m_state->db);
            query.prepare(QStringLiteral("SELECT e.account,e.folder,e.uidvalidity,e.uid,e.storage_folder FROM effective_index e "
                "JOIN message_handles h ON h.account=e.account AND h.folder=e.storage_folder AND h.uidvalidity=e.uidvalidity AND h.uid=e.uid "
                "JOIN accounts a ON a.id=e.account WHERE h.id=?"));
            query.addBindValue(handle);
            if (!query.exec()) error = query.lastError().text();
            else if (!query.next()) error = QStringLiteral("返信元メールが見つからないか、識別情報が変更されています。");
            else {
                identity = {query.value(0).toString(), query.value(1).toString(), query.value(2).toLongLong(), query.value(3).toLongLong()};
                identity.storageFolder = query.value(4).toString(); query.finish();
                QList<PendingFolder> operations; error = journal(m_state->db, identity.accountId, operations);
                if (error.isEmpty()) for (const auto &op : operations) if (underFolder(identity.folder, op.source, op.separator)) {
                    if (op.operation == QStringLiteral("rename")) identity.folder = op.destination + identity.folder.mid(op.source.size());
                    else if (op.operation == QStringLiteral("delete") || op.operation == QStringLiteral("delete-empty")) {
                        error = QStringLiteral("返信元メールのフォルダは削除されています。"); break;
                    }
                }
            }
        }
        emit messageLocated(handle, identity, error);
    }, Qt::QueuedConnection);
}
void AccountRepository::forgetMessage(const QString &handle)
{
    QMetaObject::invokeMethod(m_worker, [this, handle] {
        sql(m_state->db, QStringLiteral("DELETE FROM message_handles WHERE id=?"), {handle});
    }, Qt::QueuedConnection);
}
void AccountRepository::updateFlags(const MailRecord &identity, bool seen, bool important)
{
    QMetaObject::invokeMethod(m_worker, [this, identity, seen, important] {
        auto &db = m_state->db; QString error;
        if (!db.transaction()) { emit flagsUpdated(identity, db.lastError().text()); return; }
        const QVariantList values{(seen ? 1 : 0) | (important ? 2 : 0), identity.accountId, identity.folder, identity.uidValidity, identity.uid};
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index")}) {
            error = sql(db, QStringLiteral("UPDATE ") + table + QStringLiteral(" SET flags=? WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"), values);
            if (!error.isEmpty()) break;
        }
        if (error.isEmpty() && identity.seen != seen) error = sql(db, QStringLiteral("UPDATE cache_folders SET unread=MAX(0,unread+?) WHERE account=? AND name=?"), {seen ? -1 : 1, identity.accountId, identity.folder});
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback();
        emit flagsUpdated(identity, error);
    }, Qt::QueuedConnection);
}

void AccountRepository::queueFlag(const MailRecord &identity, bool importantFlag, bool enabled)
{
    QMetaObject::invokeMethod(m_worker, [this, identity, importantFlag, enabled] {
        auto &db = m_state->db; QString error = m_state->error;
        if (!error.isEmpty() || !db.transaction()) { emit flagsUpdated(identity, error.isEmpty() ? db.lastError().text() : error); return; }
        QString remote; bool exists = false;
        error = serverFolder(db, identity.accountId, identity.folder, remote, exists);
        if (!identity.storageFolder.isEmpty()) { remote = identity.storageFolder; exists = true; }
        if (!error.isEmpty() || !exists) { db.rollback(); emit flagsUpdated(identity, error.isEmpty() ? QStringLiteral("保留中フォルダにメールがありません。") : error); return; }
        const QVariantList key{identity.accountId, remote, identity.uidValidity, identity.uid};
        QSqlQuery query(db); query.prepare(QStringLiteral("SELECT flags FROM cache_messages WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"));
        for (const auto &value : key) query.addBindValue(value);
        if (!query.exec() || !query.next()) { db.rollback(); emit flagsUpdated(identity, QStringLiteral("フラグ変更対象のメールが見つかりません。")); return; }
        const int before = query.value(0).toInt(), mask = importantFlag ? 2 : 1;
        const int flags = (before & ~mask) | (enabled ? mask : 0); query.finish();
        QVariantList values{flags}; values.append(key);
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index")}) {
            error = sql(db, QStringLiteral("UPDATE ") + table + QStringLiteral(" SET flags=? WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"), values);
            if (!error.isEmpty()) break;
        }
        auto pending = key; pending.append(mask); pending.append(enabled ? 1 : 0); pending.append(++m_state->flagRevision);
        if (error.isEmpty()) error = sql(db, QStringLiteral("INSERT OR REPLACE INTO pending_flags VALUES(?,?,?,?,?,?,?)"), pending);
        if (error.isEmpty() && (before & 1) != (flags & 1)) error = sql(db, QStringLiteral("UPDATE cache_folders SET unread=MAX(0,unread+?) WHERE account=? AND name=?"), {enabled ? -1 : 1, identity.accountId, remote});
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback();
        emit flagsUpdated(identity, error);
    }, Qt::AutoConnection);
}
QString AccountRepository::flagRevision(qint64 &revision)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] { error = m_state->error; revision = m_state->flagRevision; }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::pendingFlags(const QString &id, const QString &folder, qint64 after, qint64 through, QList<PendingFlag> &batch)
{
    QString error; batch.clear();
    QMetaObject::invokeMethod(m_worker, [&, this] {
        error = m_state->error; if (!error.isEmpty()) return;
        QSqlQuery query(m_state->db);
        query.prepare(QStringLiteral("SELECT uidvalidity,uid,mask,enabled,revision FROM pending_flags WHERE account=? AND folder=? AND revision>? AND revision<=? ORDER BY revision LIMIT 100"));
        for (const auto &value : QVariantList{id, folder, after, through}) query.addBindValue(value);
        if (!query.exec()) { error = query.lastError().text(); return; }
        while (query.next()) batch.append({{id, folder, query.value(0).toLongLong(), query.value(1).toLongLong()}, query.value(2).toInt() == 2, query.value(3).toBool(), query.value(4).toLongLong()});
    }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::acknowledgeFlag(const PendingFlag &flag, bool serverSeen, bool serverImportant, bool exists)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        auto &db = m_state->db;
        if (!db.transaction()) { error = db.lastError().text(); return; }
        const auto &id = flag.identity;
        if (!exists) {
            for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index"), QStringLiteral("pending_flags")}) {
                error = sql(db, QStringLiteral("DELETE FROM ") + table + QStringLiteral(" WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"), {id.accountId, id.folder, id.uidValidity, id.uid});
                if (!error.isEmpty()) break;
            }
            if (error.isEmpty() && !db.commit()) error = db.lastError().text();
            if (!error.isEmpty()) db.rollback();
            return;
        }
        const int mask = flag.importantFlag ? 2 : 1;
        const QVariantList key{id.accountId, id.folder, id.uidValidity, id.uid, mask, flag.revision};
        error = sql(db, QStringLiteral("DELETE FROM pending_flags WHERE account=? AND folder=? AND uidvalidity=? AND uid=? AND mask=? AND revision=?"), key);
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index")}) {
            if (!error.isEmpty()) break;
            error = sql(db, QStringLiteral("UPDATE ") + table + QStringLiteral(" AS c SET flags=(? & ~COALESCE((SELECT SUM(mask) FROM pending_flags p WHERE p.account=c.account AND p.folder=c.folder AND p.uidvalidity=c.uidvalidity AND p.uid=c.uid),0)) | COALESCE((SELECT SUM(mask*enabled) FROM pending_flags p WHERE p.account=c.account AND p.folder=c.folder AND p.uidvalidity=c.uidvalidity AND p.uid=c.uid),0) WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"),
                {(serverSeen ? 1 : 0) | (serverImportant ? 2 : 0), id.accountId, id.folder, id.uidValidity, id.uid});
        }
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback();
    }, Qt::BlockingQueuedConnection);
    return error;
}

void AccountRepository::saveAttachment(const MailRecord &message, const QString &partId, const QString &directory)
{
    QMetaObject::invokeMethod(m_worker, [this, message, partId, directory] {
        if (!m_state->error.isEmpty()) { emit attachmentSaved({}, m_state->error); return; }
        const auto extracted = MimeParser::extractAttachment(message.document.rawSource, partId);
        if (!extracted.ok()) { emit attachmentSaved({}, extracted.error); return; }
        emit attachmentStarted(extracted.fileName.isEmpty() ? QStringLiteral("添付ファイル") : extracted.fileName);
        const auto file = saveDownloadFile(directory, extracted.fileName, extracted.data);
        if (!file.error.isEmpty()) { emit attachmentSaved({}, file.error); return; }
        DownloadEntry entry{QUuid::createUuid().toString(QUuid::WithoutBraces), message.accountId, message.storageFolder.isEmpty() ? message.folder : message.storageFolder,
            message.uidValidity, message.uid, message.document.subject, file.path, extracted.data.size(), QDateTime::currentDateTimeUtc()};
        for (const auto &attachment : message.document.attachments) if (attachment.partId == partId) entry.mimeType = attachment.mediaType;
        QSqlQuery query(m_state->db);
        query.prepare(QStringLiteral("INSERT INTO downloads VALUES(?,?,?,?,?,?,?,?,?,?)"));
        for (const auto &value : QVariantList{entry.id, entry.accountId, entry.folder, entry.uidValidity, entry.uid,
                 entry.subject, entry.path, entry.size, entry.savedAt.toString(Qt::ISODateWithMs), entry.mimeType.isNull() ? QStringLiteral("") : entry.mimeType}) query.addBindValue(value);
        if (!query.exec()) {
            emit attachmentSaved(entry, QStringLiteral("ファイルは保存しましたが履歴を保存できません: ") + query.lastError().text());
            return;
        }
        emit attachmentSaved(entry, {});
    }, Qt::QueuedConnection);
}

void AccountRepository::loadDownloadHistory()
{
    QMetaObject::invokeMethod(m_worker, [this] {
        if (!m_state->error.isEmpty()) { emit failed(QStringLiteral("history"), m_state->error); return; }
        QSqlQuery query(m_state->db);
        if (!query.exec(QStringLiteral("SELECT id,account,folder,uidvalidity,uid,subject,path,size,saved_at,mime FROM downloads ORDER BY saved_at DESC,id"))) {
            emit failed(QStringLiteral("history"), query.lastError().text()); return;
        }
        QList<DownloadEntry> entries;
        while (query.next()) entries.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
            query.value(3).toLongLong(), query.value(4).toLongLong(), query.value(5).toString(), query.value(6).toString(),
            query.value(7).toLongLong(), QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs), query.value(9).toString()});
        emit historyLoaded(entries);
    }, Qt::QueuedConnection);
}
} // namespace smoothmail
