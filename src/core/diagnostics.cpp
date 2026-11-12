#include "diagnostics.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QUuid>
#include <exception>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace smoothmail::diagnostics {
namespace {
QMutex mutex;
QFile log;
QString marker;
#ifdef Q_OS_WIN
HANDLE fatalFile = INVALID_HANDLE_VALUE;
LONG WINAPI fatal(EXCEPTION_POINTERS *exception)
{
    // No Qt, heap allocation, locks, mail data, or memory dumps in this path.
    char line[128] = "windows-exception code=0x";
    unsigned position = 25;
    const char hex[] = "0123456789abcdef";
    const auto code = exception->ExceptionRecord->ExceptionCode;
    for (int shift = 28; shift >= 0; shift -= 4) line[position++] = hex[(code >> shift) & 15];
    const char label[] = " address=0x";
    for (unsigned i = 0; i < sizeof(label) - 1; ++i) line[position++] = label[i];
    const auto address = reinterpret_cast<quintptr>(exception->ExceptionRecord->ExceptionAddress);
    for (int shift = sizeof(address) * 8 - 4; shift >= 0; shift -= 4) line[position++] = hex[(address >> shift) & 15];
    line[position++] = '\r'; line[position++] = '\n';
    DWORD written;
    WriteFile(fatalFile, line, position, &written, nullptr);
    // Record the module basename and relative offset, not an install/user path.
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(exception->ExceptionRecord->ExceptionAddress, &region, sizeof(region))) {
        char path[MAX_PATH]{};
        const auto length = GetModuleFileNameA(static_cast<HMODULE>(region.AllocationBase), path, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            unsigned start = 0;
            for (unsigned i = 0; i < length; ++i) if (path[i] == '\\' || path[i] == '/') start = i + 1;
            WriteFile(fatalFile, path + start, length - start, &written, nullptr);
            char offset[40] = " offset=0x";
            unsigned end = 10;
            const auto relative = address - reinterpret_cast<quintptr>(region.AllocationBase);
            for (int shift = sizeof(relative) * 8 - 4; shift >= 0; shift -= 4) offset[end++] = hex[(relative >> shift) & 15];
            offset[end++] = '\r'; offset[end++] = '\n';
            WriteFile(fatalFile, offset, end, &written, nullptr);
        }
    }
    FlushFileBuffers(fatalFile);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
void terminated()
{
#ifdef Q_OS_WIN
    const char message[] = "std-terminate (exception text deliberately omitted)\r\n";
    DWORD written;
    WriteFile(fatalFile, message, sizeof(message) - 1, &written, nullptr);
    FlushFileBuffers(fatalFile);
#endif
    std::abort();
}
}
bool start(const QString &directory)
{
    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral("."))) return false;
    // Each process owns its marker; a second instance cannot erase a live session.
    for (const auto &name : dir.entryList({QStringLiteral("*.active")}, QDir::Files)) {
        QFile previous(dir.filePath(name));
        if (!previous.open(QIODevice::ReadOnly)) continue;
        const auto pid = previous.read(32).trimmed().toULong();
        previous.close();
#ifdef Q_OS_WIN
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (process) {
            const auto state = WaitForSingleObject(process, 0); CloseHandle(process);
            if (state != WAIT_OBJECT_0) continue;
        } else if (GetLastError() != ERROR_INVALID_PARAMETER) continue;
#else
        Q_UNUSED(pid);
        continue;
#endif
        previous.rename(dir.filePath(name + QStringLiteral(".abnormal")));
    }
    const auto id = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-"))
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    marker = dir.filePath(id + QStringLiteral(".active"));
    log.setFileName(dir.filePath(id + QStringLiteral(".log")));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
    QFile active(marker);
    if (!active.open(QIODevice::WriteOnly)) return false;
    active.write(QByteArray::number(QCoreApplication::applicationPid())); active.close();
#ifdef Q_OS_WIN
    const auto path = QDir::toNativeSeparators(dir.filePath(id + QStringLiteral(".fatal.txt")));
    fatalFile = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (fatalFile == INVALID_HANDLE_VALUE) return false;
    SetUnhandledExceptionFilter(fatal);
#endif
    std::set_terminate(terminated);
    event("session-start");
    log.write("version=" + QCoreApplication::applicationVersion().toUtf8() + " qt=" QT_VERSION_STR " build=" __DATE__ " " __TIME__ "\n"); log.flush();
    return true;
}
void event(const char *stage, qint64 count)
{
    const QMutexLocker lock(&mutex);
    if (!log.isOpen()) return;
    // Bound disk use per session. Fatal output has a separate pre-opened handle.
    if (log.size() > 4 * 1024 * 1024) { log.resize(0); log.seek(0); }
    log.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
              + ' ' + stage + (count >= 0 ? ' ' + QByteArray::number(count) : QByteArray{}) + '\n');
    log.flush();
}
void finish()
{
    event("session-clean-exit");
    const QMutexLocker lock(&mutex);
    log.close();
    if (!marker.isEmpty()) QFile::remove(marker);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(nullptr);
    if (fatalFile != INVALID_HANDLE_VALUE) CloseHandle(fatalFile);
    fatalFile = INVALID_HANDLE_VALUE;
#endif
}
}
