#pragma once
#include "systemsettings.h"
#include "oauthdefaults.h"

namespace smoothmail {
inline QString oauthClientId(const SystemSettings &settings, const QString &provider)
{
    if (provider == QStringLiteral("google"))
        return settings.useDefaultGoogleClient ? oauthDefaults::googleClientId() : settings.googleClientId;
    if (provider == QStringLiteral("microsoft"))
        return settings.useDefaultMicrosoftClient ? oauthDefaults::microsoftClientId() : settings.microsoftClientId;
    return {};
}
inline QString oauthGoogleSecret(const SystemSettings &settings, const QString &customSecret)
{
    return settings.useDefaultGoogleClient ? oauthDefaults::googleSecret() : customSecret;
}
// Grants remain bound to their original client ID when the system default is changed.
inline QString oauthGoogleSecretForClient(const SystemSettings &settings, const QString &customSecret, const QString &clientId)
{
    if (clientId == oauthClientId(settings, QStringLiteral("google")))
        return oauthGoogleSecret(settings, customSecret);
    if (!clientId.isEmpty() && clientId == oauthDefaults::googleClientId())
        return oauthDefaults::googleSecret();
    if (!clientId.isEmpty() && clientId == settings.googleClientId) return customSecret;
    return {};
}
}
