#include "accountrepository.h"
#include "mail/mimeparser.h"

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

namespace smoothmail {
struct AccountRepository::State
{
    QSqlDatabase db;
    QString error;
    QString connection;

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
        if (version > 1) {
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
                 QStringLiteral("PRAGMA user_version=1")}) {
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
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [this, path] { m_state = new State(path); }, Qt::QueuedConnection);
    QTimer::singleShot(0, this, &AccountRepository::reload);
}

AccountRepository::~AccountRepository()
{
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
        }
        if (!db.commit()) { const auto error = db.lastError().text(); db.rollback(); fail(error); return; }
        emit snapshotSaved(id, folder, folders, messages);
    }, Qt::QueuedConnection);
}

void AccountRepository::loadCache(const QString &id)
{
    QMetaObject::invokeMethod(m_worker, [this, id] {
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
        }
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
} // namespace smoothmail
