#pragma once

#include "core/account.h"
#include <QObject>
#include <QThread>

namespace smoothmail {

// SQLite handles are created, used and destroyed only on the private worker.
class AccountRepository final : public QObject
{
    Q_OBJECT
public:
    explicit AccountRepository(const QString &databasePath, QObject *parent = nullptr);
    ~AccountRepository() override;
    void reload();
    void insert(const Account &account);
    void reorder(const QStringList &accountIds);

signals:
    void loaded(const QList<smoothmail::Account> &accounts);
    void inserted(const QString &id);
    void failed(const QString &operation, const QString &error);

private:
    struct State;
    State *m_state = nullptr;
    QObject *m_worker = nullptr;
    QThread m_thread;
};
} // namespace smoothmail
