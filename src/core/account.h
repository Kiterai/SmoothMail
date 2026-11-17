#pragma once

#include "connectionsettings.h"
#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace smoothmail {

struct ServerSettings
{
    QString host;
    quint16 port = 0;
    QString user;
    AuthenticationMode authentication = AuthenticationMode::Plain;
    EncryptionMode encryption = EncryptionMode::ImplicitTls;
    QString oauthProvider;
    QString oauthClientId;
};

// No passwords or tokens belong in these persistable values.
struct Account
{
    QString id;
    QString email;
    ServerSettings smtp;
    ServerSettings imap;
};

QString validateAccount(const Account &account, bool requireOAuthRegistration = true);
QJsonObject accountJson(const Account &account);
bool accountFromJson(const QJsonObject &json, Account &account);

} // namespace smoothmail

Q_DECLARE_METATYPE(smoothmail::Account)
Q_DECLARE_METATYPE(QList<smoothmail::Account>)
