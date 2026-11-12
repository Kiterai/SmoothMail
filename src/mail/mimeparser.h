#pragma once

#include "core/messagedocument.h"

namespace smoothmail {

struct MessageParseResult
{
    MessageDocument message;
    QString error;

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};
struct AttachmentResult {
    QByteArray data;
    QString fileName;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

// Synchronous value adapter: invoke on a mail worker, not on the GUI thread.
// No KMime object, transport session or credential escapes into the UI model.
class MimeParser final
{
public:
    static constexpr qsizetype MaxRawBytes = 64 * 1024 * 1024;
    static constexpr int MaxParts = 1024;
    static constexpr int MaxPartDepth = 32;

    [[nodiscard]] static MessageParseResult parse(const QByteArray &rawMessage);
    [[nodiscard]] static AttachmentResult extractAttachment(const QByteArray &rawMessage, const QString &partId);
};

} // namespace smoothmail
