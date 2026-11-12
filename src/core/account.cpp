#include "account.h"

#include <QRegularExpression>
#include <QUuid>

namespace smoothmail {
namespace {
bool noControls(const QString &text)
{
    for (const auto c : text) {
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) {
            return false;
        }
    }
    return true;
}

QJsonObject serverJson(const ServerSettings &server)
{
    return {{"host", server.host}, {"port", server.port}, {"user", server.user},
            {"authentication", int(server.authentication)}, {"encryption", int(server.encryption)},
            {"oauthProvider", server.oauthProvider}, {"oauthClientId", server.oauthClientId}};
}

bool readServer(const QJsonObject &json, ServerSettings &server)
{
    const auto port = json.value("port").toInt(-1);
    if (!json.value("port").isDouble() || json.value("port").toDouble() != port || port < 1 || port > 65535) {
        return false;
    }
    server.host = json.value("host").toString();
    server.user = json.value("user").toString();
    server.port = quint16(port);
    server.oauthProvider = json.value("oauthProvider").toString();
    server.oauthClientId = json.value("oauthClientId").toString();
    for (const auto &key : {QStringLiteral("authentication"), QStringLiteral("encryption")}) {
        const auto value = json.value(key);
        if (!value.isDouble() || value.toDouble() != value.toInt(-1)) {
            return false;
        }
    }
    server.authentication = static_cast<AuthenticationMode>(json.value("authentication").toInt(-1));
    server.encryption = static_cast<EncryptionMode>(json.value("encryption").toInt(-1));
    return true;
}
}

QString validateAccount(const Account &account, bool requireOAuthRegistration)
{
    if (QUuid(account.id).isNull()) {
        return QStringLiteral("アカウントIDが不正です。");
    }
    static const QRegularExpression address(QStringLiteral(R"(^[^\s<>@,;]+@[^\s<>@,;]+$)"));
    if (account.email.size() > 320 || !noControls(account.email) || !address.match(account.email).hasMatch()) {
        return QStringLiteral("メールアドレスを正しく入力してください。");
    }
    for (const auto *server : {&account.smtp, &account.imap}) {
        if (server->host.isEmpty() || server->host.size() > 253 || !noControls(server->host)
            || server->host.contains(QRegularExpression(QStringLiteral("[\\s/@?#]"))) || !server->port
            || server->user.isEmpty() || !noControls(server->user) || server->user.size() > 1024) {
            return QStringLiteral("サーバ、ポート番号、アカウント名を正しく入力してください。");
        }
        switch (server->authentication) {
        case AuthenticationMode::Plain: case AuthenticationMode::Login: case AuthenticationMode::OAuth2: break;
        default: return QStringLiteral("認証方式が不正です。");
        }
        switch (server->encryption) {
        case EncryptionMode::None: case EncryptionMode::ImplicitTls: case EncryptionMode::StartTls: break;
        default: return QStringLiteral("暗号化方式が不正です。");
        }
        if (requireOAuthRegistration && server->authentication == AuthenticationMode::OAuth2
            && ((server->oauthProvider != QStringLiteral("google") && server->oauthProvider != QStringLiteral("microsoft"))
                || server->oauthClientId.trimmed().isEmpty() || !noControls(server->oauthClientId))) {
            return QStringLiteral("OAuthのプロバイダとアプリ登録情報を設定し、認可してください。");
        }
    }
    return {};
}

QJsonObject accountJson(const Account &account)
{
    return {{"id", account.id}, {"email", account.email},
            {"smtp", serverJson(account.smtp)}, {"imap", serverJson(account.imap)}};
}

bool accountFromJson(const QJsonObject &json, Account &account)
{
    Account value;
    value.id = json.value("id").toString();
    value.email = json.value("email").toString();
    if (!readServer(json.value("smtp").toObject(), value.smtp)
        || !readServer(json.value("imap").toObject(), value.imap) || !validateAccount(value).isEmpty()) {
        return false;
    }
    account = value;
    return true;
}
} // namespace smoothmail
