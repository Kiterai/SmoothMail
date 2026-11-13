#include "accountrepository.h"
#include "mail/mimeparser.h"
#include "downloadfile.h"
#include "core/diagnostics.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>

namespace smoothmail {
namespace {
QString sql(QSqlDatabase &db, const QString &statement, const QVariantList &values = {})
{
    QSqlQuery query(db); query.prepare(statement);
    for (const auto &value : values) query.addBindValue(value);
    return query.exec() ? QString{} : query.lastError().text();
}
QJsonArray identifiers(const QList<QByteArray> &values)
{
    QJsonArray result; for (const auto &value : values) result.append(QString::fromLatin1(value)); return result;
}
QString indexMessage(QSqlDatabase &db, const QString &table, const MailRecord &record)
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
    return sql(db, QStringLiteral("INSERT OR REPLACE INTO ") + table + QStringLiteral(" VALUES(?,?,?,?,?,?,?)"),
        {record.accountId, record.folder, record.uidValidity, record.uid, (record.seen ? 1 : 0) | (record.important ? 2 : 0),
         QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)), search.toCaseFolded()});
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
    return record;
}
}
struct AccountRepository::State
{
    QSqlDatabase db;
    QString error;
    QString connection;
    QString syncId, syncFolder;
    qint64 syncValidity = 0;

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
        if (version > 2) {
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
                 QStringLiteral("CREATE TABLE IF NOT EXISTS downloads (id TEXT PRIMARY KEY,account TEXT,folder TEXT,uidvalidity INTEGER,uid INTEGER,subject TEXT,path TEXT,size INTEGER,saved_at TEXT)"),
                  QStringLiteral("PRAGMA user_version=2")}) {
            if (!query.exec(sql)) {
                error = query.lastError().text();
                db.rollback();
                return;
            }
        }
        if (!db.commit()) {
            error = db.lastError().text();
            db.rollback();
        }
        QSqlQuery temporary(db); temporary.exec(QStringLiteral("PRAGMA temp_store=FILE"));
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
    QMetaObject::invokeMethod(m_worker, [this] {
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
        query.prepare(QStringLiteral("UPDATE accounts SET config=? WHERE id=?"));
        query.addBindValue(QJsonDocument(accountJson(account)).toJson(QJsonDocument::Compact));
        query.addBindValue(account.id);
        if (!query.exec() || query.numRowsAffected() != 1) { db.rollback(); fail(QStringLiteral("設定を更新できません。")); return; }
        // Changed servers/users must never reuse another mailbox's UID cache.
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index"), QStringLiteral("cache_folders")}) {
            query.prepare(QStringLiteral("DELETE FROM ") + table + QStringLiteral(" WHERE account=?"));
            query.addBindValue(account.id);
            if (!query.exec()) { db.rollback(); fail(query.lastError().text()); return; }
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
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index"), QStringLiteral("cache_folders"), QStringLiteral("accounts")}) {
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
        diagnostics::event("cache-save-start", messages.size());
        auto &db = m_state->db;
        const auto fail = [this, id](const QString &error) { emit failed(QStringLiteral("snapshot:") + id, error); };
        if (!m_state->error.isEmpty()) { fail(m_state->error); return; }
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT id FROM accounts WHERE id=?"));
        query.addBindValue(id);
        if (!query.exec() || !query.next()) { fail(QStringLiteral("保存先アカウントが見つかりません。")); return; }
        query.finish();
        if (!db.transaction()) { fail(db.lastError().text()); return; }
        const auto exec = [&](const QString &sql, const QVariantList &values) {
            query.prepare(sql);
            for (const auto &value : values) query.addBindValue(value);
            if (query.exec()) return true;
            const auto error = query.lastError().text(); db.rollback(); fail(error); return false;
        };
        if (!exec(QStringLiteral("DELETE FROM cache_folders WHERE account=?"), {id})) return;
        QSet<QString> names;
        for (const auto &item : folders) {
            if (item.name.isEmpty() || names.contains(item.name) || item.unread < 0) {
                db.rollback(); fail(QStringLiteral("フォルダ情報が不正です。")); return;
            }
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
                db.rollback(); fail(QStringLiteral("メッセージのUIDまたはデータが不正です。")); return;
            }
            validity = message.uidValidity;
            uids.insert(message.uid);
            if (!exec(QStringLiteral("INSERT INTO cache_messages VALUES(?,?,?,?,?,?)"),
                {id, folder, validity, message.uid, (message.seen ? 1 : 0) | (message.important ? 2 : 0), message.document.rawSource})) return;
            const auto indexError = indexMessage(db, QStringLiteral("cache_index"), message);
            if (!indexError.isEmpty()) { db.rollback(); fail(indexError); return; }
        }
        if (!db.commit()) { const auto error = db.lastError().text(); db.rollback(); fail(error); return; }
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
            emit cacheLoaded(id, folders.isEmpty() ? QString{} : folders.first().name, folders, {}); return;
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
QString AccountRepository::beginSync(const QString &id, const QString &folder, qint64 validity)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        auto &db = m_state->db;
        error = m_state->error;
        if (!error.isEmpty()) return;
        for (const auto &statement : {QStringLiteral("DROP TABLE IF EXISTS temp.sync_messages"), QStringLiteral("DROP TABLE IF EXISTS temp.sync_index"),
             QStringLiteral("CREATE TEMP TABLE sync_messages AS SELECT * FROM cache_messages WHERE 0"),
             QStringLiteral("CREATE UNIQUE INDEX temp.sync_uid ON sync_messages(uid)"),
             QStringLiteral("CREATE TEMP TABLE sync_index AS SELECT * FROM cache_index WHERE 0")}) {
            error = sql(db, statement); if (!error.isEmpty()) return;
        }
        m_state->syncId = id; m_state->syncFolder = folder; m_state->syncValidity = validity;
    }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::stageMessages(const QList<MailRecord> &messages)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        auto &db = m_state->db;
        if (messages.size() > MailPageSize || !db.transaction()) { error = QStringLiteral("同期バッチを開始できません。"); return; }
        for (const auto &record : messages) {
            if (record.accountId != m_state->syncId || record.folder != m_state->syncFolder || record.uidValidity != m_state->syncValidity
                || record.uid <= 0 || record.document.rawSource.isEmpty() || record.document.rawSource.size() > MimeParser::MaxRawBytes) {
                error = QStringLiteral("同期バッチの識別情報が不正です。"); break;
            }
            error = sql(db, QStringLiteral("INSERT INTO sync_messages VALUES(?,?,?,?,?,?)"), {record.accountId, record.folder,
                record.uidValidity, record.uid, (record.seen ? 1 : 0) | (record.important ? 2 : 0), record.document.rawSource});
            if (!error.isEmpty()) break;
            error = indexMessage(db, QStringLiteral("sync_index"), record); if (!error.isEmpty()) break;
        }
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback();
    }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::reuseMessage(qint64 uid, bool seen, bool important, bool &found)
{
    QString error; found = false;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        auto &db = m_state->db;
        const QVariantList identity{m_state->syncId, m_state->syncFolder, m_state->syncValidity, uid};
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT 1 FROM cache_index WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"));
        for (const auto &value : identity) query.addBindValue(value);
        if (!query.exec()) { error = query.lastError().text(); return; }
        if (!query.next()) return;
        query.finish();
        if (!db.transaction()) { error = db.lastError().text(); return; }
        QVariantList values{(seen ? 1 : 0) | (important ? 2 : 0)}; values.append(identity);
        error = sql(db, QStringLiteral("INSERT INTO sync_messages SELECT account,folder,uidvalidity,uid,?,raw FROM cache_messages WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"), values);
        if (error.isEmpty()) error = sql(db, QStringLiteral("INSERT INTO sync_index SELECT account,folder,uidvalidity,uid,?,meta,search_text FROM cache_index WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"), values);
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback(); else found = true;
    }, Qt::BlockingQueuedConnection);
    return error;
}
QString AccountRepository::commitSync(const QList<MailFolder> &folders, qint64 expectedCount)
{
    QString error;
    QMetaObject::invokeMethod(m_worker, [&, this] {
        auto &db = m_state->db;
        QSqlQuery count(db);
        if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM sync_messages")) || !count.next() || count.value(0).toLongLong() != expectedCount) {
            error = QStringLiteral("同期中にメール件数が変化しました。再試行してください。"); return;
        }
        count.finish();
        if (!db.transaction()) { error = db.lastError().text(); return; }
        const auto run = [&](const QString &statement, const QVariantList &values = {}) {
            error = sql(db, statement, values); return error.isEmpty();
        };
        const auto id = m_state->syncId, folder = m_state->syncFolder;
        QSqlQuery account(db); account.prepare(QStringLiteral("SELECT 1 FROM accounts WHERE id=?")); account.addBindValue(id);
        if (!account.exec() || !account.next()) { db.rollback(); error = QStringLiteral("同期先アカウントがありません。"); return; } account.finish();
        if (!run(QStringLiteral("DELETE FROM cache_folders WHERE account=?"), {id})) { db.rollback(); return; }
        QSet<QString> names;
        for (const auto &item : folders) {
            if (item.name.isEmpty() || names.contains(item.name) || item.unread < 0) { error = QStringLiteral("フォルダ情報が不正です。"); break; }
            names.insert(item.name); QJsonArray flags; for (const auto &flag : item.flags) flags.append(QString::fromLatin1(flag));
            if (!run(QStringLiteral("INSERT INTO cache_folders VALUES(?,?,?,?,?)"), {id, item.name, item.separator.isNull() ? QString{} : QString(item.separator), QJsonDocument(flags).toJson(QJsonDocument::Compact), item.unread})) break;
        }
        if (!error.isEmpty()) { db.rollback(); return; }
        for (const auto &table : {QStringLiteral("cache_messages"), QStringLiteral("cache_index")}) {
            if (!run(QStringLiteral("DELETE FROM ") + table + QStringLiteral(" WHERE account=? AND (folder=? OR NOT EXISTS (SELECT 1 FROM cache_folders WHERE cache_folders.account=") + table + QStringLiteral(".account AND cache_folders.name=") + table + QStringLiteral(".folder))"), {id, folder})) break;
        }
        if (error.isEmpty()) run(QStringLiteral("INSERT INTO cache_messages SELECT * FROM sync_messages"));
        if (error.isEmpty()) run(QStringLiteral("INSERT INTO cache_index SELECT * FROM sync_index"));
        if (error.isEmpty() && !db.commit()) error = db.lastError().text();
        if (!error.isEmpty()) db.rollback();
        diagnostics::event(error.isEmpty() ? "sync-cache-commit" : "sync-cache-rollback", expectedCount);
    }, Qt::BlockingQueuedConnection);
    return error;
}
void AccountRepository::abortSync()
{
    QMetaObject::invokeMethod(m_worker, [this] {
        sql(m_state->db, QStringLiteral("DROP TABLE IF EXISTS temp.sync_messages"));
        sql(m_state->db, QStringLiteral("DROP TABLE IF EXISTS temp.sync_index"));
        m_state->syncId.clear(); m_state->syncFolder.clear();
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
        if (!m_state->error.isEmpty()) { deliverError(m_state->error); return; }
        // Upgrade v1 caches one message at a time, without ever loading a folder.
        QSqlQuery missing(db);
        missing.prepare(QStringLiteral("SELECT c.uidvalidity,c.uid,c.flags,c.raw FROM cache_messages c WHERE c.account=? AND c.folder=? AND NOT EXISTS (SELECT 1 FROM cache_index i WHERE i.account=c.account AND i.folder=c.folder AND i.uidvalidity=c.uidvalidity AND i.uid=c.uid)"));
        missing.addBindValue(request.accountId); missing.addBindValue(request.folder);
        if (!missing.exec()) { deliverError(missing.lastError().text()); return; }
        while (missing.next()) {
            if (serial != m_pageRequest.load() || m_thread.isInterruptionRequested()) return;
            const auto parsed = MimeParser::parse(missing.value(3).toByteArray());
            if (!parsed.ok()) { deliverError(parsed.error); return; }
            const int flags = missing.value(2).toInt();
            const MailRecord record{request.accountId, request.folder, missing.value(0).toLongLong(), missing.value(1).toLongLong(), bool(flags & 1), bool(flags & 2), parsed.message};
            const auto error = indexMessage(db, QStringLiteral("cache_index"), record);
            if (!error.isEmpty()) { deliverError(error); return; }
        }
        missing.finish();
        QString where = QStringLiteral(" WHERE account=? AND folder=?");
        QVariantList values{request.accountId, request.folder};
        if (!request.text.isEmpty()) { where += QStringLiteral(" AND instr(search_text,?)>0"); values.append(request.text.toCaseFolded()); }
        if (request.importantOnly) where += QStringLiteral(" AND (flags & 2)!=0");
        if (request.unreadOnly) where += QStringLiteral(" AND (flags & 1)=0");
        const QStringList columns{QStringLiteral("json_extract(meta,'$.subject') COLLATE NOCASE"), QStringLiteral("json_extract(meta,'$.name') COLLATE NOCASE"),
            QStringLiteral("json_extract(meta,'$.address') COLLATE NOCASE"), QStringLiteral("json_extract(meta,'$.date')")};
        const auto order = QStringLiteral(" ORDER BY ") + columns.value(request.sortColumn - 3, columns.last())
            + (request.order == Qt::AscendingOrder ? QStringLiteral(" ASC,uid ASC") : QStringLiteral(" DESC,uid DESC"));
        const auto execute = [&](QSqlQuery &query, const QString &statement, const QVariantList &bindings) {
            query.prepare(statement); for (const auto &value : bindings) query.addBindValue(value); return query.exec();
        };
        QSqlQuery query(db);
        if (!execute(query, QStringLiteral("SELECT COUNT(*) FROM cache_index") + where, values) || !query.next()) { deliverError(query.lastError().text()); return; }
        page.total = query.value(0).toLongLong(); query.finish();
        if (request.revealUid > 0) {
            auto bindings = values; bindings.append(request.revealUid); bindings.append(request.revealValidity);
            if (!execute(query, QStringLiteral("SELECT position FROM (SELECT uid,uidvalidity,ROW_NUMBER() OVER(") + order + QStringLiteral(") AS position FROM cache_index")
                + where + QStringLiteral(") WHERE uid=? AND uidvalidity=?"), bindings)) { deliverError(query.lastError().text()); return; }
            if (query.next()) page.query.page = int((query.value(0).toLongLong() - 1) / MailPageSize + 1);
            query.finish();
        }
        page.query.page = qBound(1, page.query.page, int(qMax<qint64>(1, (page.total + MailPageSize - 1) / MailPageSize)));
        auto bindings = values; bindings.append(MailPageSize); bindings.append(qint64(page.query.page - 1) * MailPageSize);
        if (!execute(query, QStringLiteral("SELECT uidvalidity,uid,flags,meta FROM cache_index") + where + order + QStringLiteral(" LIMIT ? OFFSET ?"), bindings)) { deliverError(query.lastError().text()); return; }
        while (query.next()) page.messages.append(summaryRecord(query, request.accountId, request.folder));
        if (serial == m_pageRequest.load()) emit pageLoaded(page);
    }, Qt::QueuedConnection);
}
void AccountRepository::loadMessage(const MailRecord &identity)
{
    const auto serial = ++m_bodyRequest;
    QMetaObject::invokeMethod(m_worker, [this, identity, serial] {
        if (serial != m_bodyRequest.load()) return;
        QSqlQuery query(m_state->db);
        query.prepare(QStringLiteral("SELECT raw,flags FROM cache_messages WHERE account=? AND folder=? AND uidvalidity=? AND uid=?"));
        for (const auto &value : QVariantList{identity.accountId, identity.folder, identity.uidValidity, identity.uid}) query.addBindValue(value);
        if (!query.exec() || !query.next()) { emit messageLoaded(identity, QStringLiteral("メッセージが見つかりません。")); return; }
        auto record = identity; const auto parsed = MimeParser::parse(query.value(0).toByteArray());
        record.document = parsed.message; record.seen = query.value(1).toInt() & 1; record.important = query.value(1).toInt() & 2;
        if (serial == m_bodyRequest.load()) emit messageLoaded(record, parsed.error);
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

void AccountRepository::saveAttachment(const MailRecord &message, const QString &partId, const QString &directory)
{
    QMetaObject::invokeMethod(m_worker, [this, message, partId, directory] {
        if (!m_state->error.isEmpty()) { emit attachmentSaved({}, m_state->error); return; }
        const auto extracted = MimeParser::extractAttachment(message.document.rawSource, partId);
        if (!extracted.ok()) { emit attachmentSaved({}, extracted.error); return; }
        emit attachmentStarted(extracted.fileName.isEmpty() ? QStringLiteral("添付ファイル") : extracted.fileName);
        const auto file = saveDownloadFile(directory, extracted.fileName, extracted.data);
        if (!file.error.isEmpty()) { emit attachmentSaved({}, file.error); return; }
        const DownloadEntry entry{QUuid::createUuid().toString(QUuid::WithoutBraces), message.accountId, message.folder,
            message.uidValidity, message.uid, message.document.subject, file.path, extracted.data.size(), QDateTime::currentDateTimeUtc()};
        QSqlQuery query(m_state->db);
        query.prepare(QStringLiteral("INSERT INTO downloads VALUES(?,?,?,?,?,?,?,?,?)"));
        for (const auto &value : QVariantList{entry.id, entry.accountId, entry.folder, entry.uidValidity, entry.uid,
                 entry.subject, entry.path, entry.size, entry.savedAt.toString(Qt::ISODateWithMs)}) query.addBindValue(value);
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
        if (!query.exec(QStringLiteral("SELECT id,account,folder,uidvalidity,uid,subject,path,size,saved_at FROM downloads ORDER BY saved_at DESC,id"))) {
            emit failed(QStringLiteral("history"), query.lastError().text()); return;
        }
        QList<DownloadEntry> entries;
        while (query.next()) entries.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
            query.value(3).toLongLong(), query.value(4).toLongLong(), query.value(5).toString(), query.value(6).toString(),
            query.value(7).toLongLong(), QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs)});
        emit historyLoaded(entries);
    }, Qt::QueuedConnection);
}
} // namespace smoothmail
