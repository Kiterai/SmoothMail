#pragma once

#include <QString>
#include <QStringList>

namespace smoothmail {

enum class EncryptionMode { None, ImplicitTls, StartTls };
enum class AuthenticationMode { Plain, Login, OAuth2 };

struct TransportSecurity
{
    bool tlsActive = false;
    bool certificateVerified = false;
};

struct AuthenticationDecision
{
    QString mechanism;
    QString error;

    [[nodiscard]] bool allowed() const { return !mechanism.isEmpty() && error.isEmpty(); }
};

// Evaluate before creating a KIMAP/KSMTP LoginJob or supplying any secret.
// This gate never chooses an alternate mechanism or encryption mode.
[[nodiscard]] AuthenticationDecision authenticationDecision(
    EncryptionMode encryption, AuthenticationMode authentication,
    const TransportSecurity &transport, const QStringList &advertisedMechanisms);

} // namespace smoothmail
