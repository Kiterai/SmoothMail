#pragma once
#include "mailrecord.h"
namespace smoothmail {
struct PendingMove { qint64 revision = 0; QString handle; MailRecord identity; QString destination; bool attempted = false; };
}
