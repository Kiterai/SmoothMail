#pragma once
#include "mailrecord.h"

namespace smoothmail {
struct PendingFlag {
    MailRecord identity;
    bool importantFlag = false;
    bool enabled = false;
    qint64 revision = 0;
};
}
