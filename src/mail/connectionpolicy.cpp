#include "connectionpolicy.h"

namespace smoothmail {

AuthenticationDecision authenticationDecision(EncryptionMode encryption,
                                                AuthenticationMode authentication,
                                                const TransportSecurity &transport,
                                                const QStringList &advertisedMechanisms)
{
    switch (encryption) {
    case EncryptionMode::None:
        break; // Only an explicitly configured unencrypted connection permits this.
    case EncryptionMode::ImplicitTls:
    case EncryptionMode::StartTls:
        if (!transport.tlsActive) {
            return {{}, QStringLiteral("指定したTLS接続が確立していないため認証できません。")};
        }
        break;
    default:
        return {{}, QStringLiteral("不明な暗号化方式です。")};
    }
    if (transport.tlsActive && !transport.certificateVerified) {
        return {{}, QStringLiteral("TLS証明書の検証に成功していないため認証できません。")};
    }

    QString mechanism;
    switch (authentication) {
    case AuthenticationMode::Plain:
        mechanism = QStringLiteral("PLAIN");
        break;
    case AuthenticationMode::Login:
        mechanism = QStringLiteral("LOGIN");
        break;
    case AuthenticationMode::OAuth2:
        mechanism = QStringLiteral("XOAUTH2");
        break;
    default:
        return {{}, QStringLiteral("不明な認証方式です。")};
    }
    // Match the exact standard mechanism name, as KSMTP's selection does.
    // A looser match here could allow its LOGIN/PLAIN fallback to run later.
    if (!advertisedMechanisms.contains(mechanism)) {
        return {{}, QStringLiteral("サーバは指定された認証方式 %1 を提供していません。")
                         .arg(mechanism)};
    }
    return {mechanism, {}};
}

} // namespace smoothmail
