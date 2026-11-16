#pragma once
#include <QLockFile>
#include <QDir>

namespace smoothmail {
// Kept alive until application shutdown. QLockFile also recovers dead owners.
class SingleInstance final {
public:
    explicit SingleInstance(const QString &dataPath)
        : m_lock(dataPath + QStringLiteral("/smoothmail.lock"))
    {
        m_lock.setStaleLockTime(0); // A long-running tray process is not stale.
        m_directoryReady = QDir().mkpath(dataPath);
    }
    bool acquire() { return m_directoryReady && m_lock.tryLock(0); }
    bool alreadyRunning() const { return m_lock.error() == QLockFile::LockFailedError; }
private:
    QLockFile m_lock;
    bool m_directoryReady = false;
};
}
