#pragma once
#include <mutex>

namespace smoothmail {
// Counts reserved primary connections (including authentication waits) and live
// helper connections together. A queued account takes priority over helpers.
class SyncBudget final
{
public:
    explicit SyncBudget(int limit) : m_limit(limit) {}
    void setLimit(int limit) { std::lock_guard lock(m_mutex); m_limit = limit; }
    void setPrimaryWaiting(bool waiting) { std::lock_guard lock(m_mutex); m_primaryWaiting = waiting; }
    bool primaryWaiting() const { std::lock_guard lock(m_mutex); return m_primaryWaiting; }
    bool reserve(bool helper = false) {
        std::lock_guard lock(m_mutex);
        if (m_used >= m_limit || (helper && m_primaryWaiting)) return false;
        ++m_used; return true;
    }
    void release() { std::lock_guard lock(m_mutex); --m_used; }
private:
    mutable std::mutex m_mutex;
    int m_limit, m_used = 0;
    bool m_primaryWaiting = false;
};
}
