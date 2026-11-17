#pragma once

namespace smoothmail {

// Persistable connection choices shared by account models and transport policy.
enum class EncryptionMode { None, ImplicitTls, StartTls };
enum class AuthenticationMode { Plain, Login, OAuth2 };

} // namespace smoothmail
