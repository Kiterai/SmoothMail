#pragma once
#include <QString>
namespace smoothmail::diagnostics {
// Only fixed, developer-defined event names and numeric counters are accepted.
using OperationId = quint64;
OperationId nextOperation();
OperationId currentOperation();
class OperationScope final {
public:
    explicit OperationScope(OperationId operation);
    ~OperationScope();
    OperationScope(const OperationScope &) = delete;
    OperationScope &operator=(const OperationScope &) = delete;
private:
    OperationId m_previous;
};
enum class FolderWriteSource { Snapshot, SyncCommit };
// Numeric summaries only: never pass folder/account names, hashes or responses.
struct FolderValidationFailure {
    qint64 total = 0, emptyCount = 0, duplicateCount = 0, negativeUnreadCount = 0;
    qint64 firstEmptyIndex = 0, firstDuplicateIndex = 0, duplicateOfIndex = 0, firstNegativeIndex = 0;
    qint64 firstNegativeUnread = 0;
    bool firstEmptyNoSelect = false;
};
void folderValidationFailure(FolderWriteSource source, const FolderValidationFailure &failure);
bool start(const QString &directory);
void event(const char *stage, qint64 count = -1);
void finish();
}
