#pragma once
#include <QString>
namespace smoothmail::diagnostics {
// Only fixed, developer-defined event names and numeric counters are accepted.
bool start(const QString &directory);
void event(const char *stage, qint64 count = -1);
void finish();
}
