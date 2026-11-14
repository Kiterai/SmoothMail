#include "diagnostics.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QUuid>
#include <exception>
#include <cstdlib>
#include <csignal>
#include <QCryptographicHash>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <atomic>
#ifdef Q_OS_WIN
#include <windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif
#endif

namespace smoothmail::diagnostics {
namespace {
QMutex mutex;
QFile log;
QString marker;
std::terminate_handler previousTerminate = nullptr;
QtMessageHandler previousMessage = nullptr;
bool sessionActive = false;
std::atomic<OperationId> operationSerial{0};
thread_local OperationId operationId = 0;
thread_local bool writingDiagnostic = false;
void appendRecord(const QByteArray &entry)
{
    if (writingDiagnostic) return;
    const QScopedValueRollback<bool> guard(writingDiagnostic, true);
    const QMutexLocker lock(&mutex);
    if (!log.isOpen()) return;
    // Bound disk use per session. Fatal output has a separate pre-opened handle.
    if (log.size() > 4 * 1024 * 1024) { log.resize(0); log.seek(0); }
    log.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
        + ' ' + entry + (operationId ? " op=" + QByteArray::number(operationId) : QByteArray{}) + '\n');
    log.flush();
}
const char *qtStage(QtMsgType type, const char *category)
{
    const bool warning = type == QtWarningMsg;
    if (category && qstrncmp(category, "org.kde.pim.kimap", 17) == 0) return warning ? "qt-warning-kimap" : "qt-critical-kimap";
    if (category && qstrncmp(category, "org.kde.pim.ksmtp", 17) == 0) return warning ? "qt-warning-ksmtp" : "qt-critical-ksmtp";
    if (category && qstrncmp(category, "qt.sql", 6) == 0) return warning ? "qt-warning-sql" : "qt-critical-sql";
    if (category && qstrncmp(category, "qt.network", 10) == 0) return warning ? "qt-warning-network" : "qt-critical-network";
    return warning ? "qt-warning-other" : "qt-critical-other";
}
#ifdef Q_OS_WIN
HANDLE fatalFile = INVALID_HANDLE_VALUE;
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
LONG recording = 0;
using AbortHandler = void (*)(int);
AbortHandler previousAbort = SIG_DFL;
void writeFixed(const char *text)
{
    DWORD size = 0, written;
    while (text[size]) ++size;
    WriteFile(fatalFile, text, size, &written, nullptr);
}
void writeNumber(const char *label, quintptr value)
{
    char buffer[128]; unsigned position = 0; const char digits[] = "0123456789abcdef";
    while (*label && position < 80) buffer[position++] = *label++;
    for (int shift = sizeof(value) * 8 - 4; shift >= 0; shift -= 4) buffer[position++] = digits[(value >> shift) & 15];
    buffer[position++] = '\r'; buffer[position++] = '\n'; DWORD written;
    WriteFile(fatalFile, buffer, position, &written, nullptr);
}
void recordStack(const char *reason)
{
    // Only code addresses/module basenames. Never write stack memory or report
    // text: either can contain credentials, server replies, or mail content.
    if (InterlockedCompareExchange(&recording, 1, 0)) return;
    writeFixed(reason); writeFixed("\r\n"); writeNumber("thread=0x", GetCurrentThreadId());
    void *frames[40]{}; const auto count = CaptureStackBackTrace(1, 40, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        MEMORY_BASIC_INFORMATION region{}; char path[MAX_PATH]{};
        const auto address = reinterpret_cast<quintptr>(frames[i]);
        if (!VirtualQuery(frames[i], &region, sizeof(region))) { writeNumber("unknown address=0x", address); continue; }
        const auto length = GetModuleFileNameA(static_cast<HMODULE>(region.AllocationBase), path, MAX_PATH);
        if (!length || length >= MAX_PATH) { writeNumber("unknown address=0x", address); continue; }
        unsigned start = 0; for (unsigned j = 0; j < length; ++j) if (path[j] == '\\' || path[j] == '/') start = j + 1;
        DWORD written; WriteFile(fatalFile, path + start, length - start, &written, nullptr);
        writeNumber(" offset=0x", address - reinterpret_cast<quintptr>(region.AllocationBase));
    }
    FlushFileBuffers(fatalFile); InterlockedExchange(&recording, 0);
}
void aborted(int)
{
    // Release CRT aborts do not use the Debug-only report hooks. Keep this
    // path independent of Qt, allocation, report text and the CRT dialog.
    recordStack("crt-abort");
    TerminateProcess(GetCurrentProcess(), 0x40000015);
}
#if defined(_MSC_VER) && defined(_DEBUG)
template<typename Character> bool containsFixed(const Character *text, const Character *needle)
{
    if (!text) return false;
    for (unsigned i = 0; i < 8192 && text[i]; ++i) {
        unsigned j = 0; while (needle[j] && i + j < 8192 && text[i + j] && text[i + j] == needle[j]) ++j;
        if (!needle[j]) return true;
    }
    return false;
}
int __cdecl crtReport(int type, char *message, int *)
{
    if (type != _CRT_ASSERT) return FALSE;
    recordStack(containsFixed(message, "c >= -1") && containsFixed(message, "c <= 255") ? "crt-assert-ctype-domain" : "crt-assert");
    // Do not ignore an assertion or continue with potentially corrupted state.
    // Avoid the CRT report dialog and recursive report/breakpoint path.
    TerminateProcess(GetCurrentProcess(), 0xc0000420); return TRUE;
}
int __cdecl crtReportWide(int type, wchar_t *message, int *)
{
    if (type != _CRT_ASSERT) return FALSE;
    recordStack(containsFixed(message, L"c >= -1") && containsFixed(message, L"c <= 255") ? "crt-assert-ctype-domain" : "crt-assert");
    TerminateProcess(GetCurrentProcess(), 0xc0000420); return TRUE;
}
#endif
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
    recordStack("windows-exception-stack");
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
void qtMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (!writingDiagnostic && (type == QtWarningMsg || type == QtCriticalMsg)) {
        // Categories are mapped to a fixed allowlist; neither arbitrary category
        // strings nor the message/file/function can be persisted.
        event(qtStage(type, context.category), context.line);
    }
#ifdef Q_OS_WIN
    if (type == QtFatalMsg) {
        recordStack("qt-fatal");
        // Qt's Debug abort path may open a blocking CRT dialog. A fatal
        // message must still terminate, never continue or echo its text.
        TerminateProcess(GetCurrentProcess(), 0x40000015);
        return;
    }
#endif
    // Keep an explicitly installed host handler (for example Qt Test). Do not
    // persist or default-print arbitrary parser/server diagnostic text.
    if (previousMessage) previousMessage(type, context, message);
}
void terminated()
{
#ifdef Q_OS_WIN
    const char message[] = "std-terminate (exception text deliberately omitted)\r\n";
    DWORD written;
    WriteFile(fatalFile, message, sizeof(message) - 1, &written, nullptr);
    FlushFileBuffers(fatalFile);
    recordStack("std-terminate-stack");
#endif
    std::abort();
}
}
OperationId nextOperation() { return ++operationSerial; }
OperationId currentOperation() { return operationId; }
OperationScope::OperationScope(OperationId operation) : m_previous(operationId) { operationId = operation; }
OperationScope::~OperationScope() { operationId = m_previous; }
void folderValidationFailure(FolderWriteSource source, const FolderValidationFailure &failure)
{
    const char *origin = source == FolderWriteSource::Snapshot ? "snapshot" : source == FolderWriteSource::SyncCommit ? "sync-commit" : "unknown";
    appendRecord(QByteArray("folder-metadata-invalid source=") + origin
        + " total=" + QByteArray::number(failure.total)
        + " empty_count=" + QByteArray::number(failure.emptyCount)
        + " duplicate_count=" + QByteArray::number(failure.duplicateCount)
        + " negative_unread_count=" + QByteArray::number(failure.negativeUnreadCount)
        + " first_empty_index=" + QByteArray::number(failure.firstEmptyIndex)
        + " first_empty_noselect=" + QByteArray::number(int(failure.firstEmptyNoSelect))
        + " first_duplicate_index=" + QByteArray::number(failure.firstDuplicateIndex)
        + " duplicate_of_index=" + QByteArray::number(failure.duplicateOfIndex)
        + " first_negative_index=" + QByteArray::number(failure.firstNegativeIndex)
        + " first_negative_unread=" + QByteArray::number(failure.firstNegativeUnread));
}
bool start(const QString &directory)
{
    if (log.isOpen()) return false;
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
    previousFilter = SetUnhandledExceptionFilter(fatal);
    previousAbort = std::signal(SIGABRT, aborted);
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, crtReport);
    _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, crtReportWide);
#endif
#endif
    previousTerminate = std::set_terminate(terminated);
    previousMessage = qInstallMessageHandler(qtMessage);
    sessionActive = true;
    event("session-start");
    log.write("version=" + QCoreApplication::applicationVersion().toUtf8() + " qt=" QT_VERSION_STR " build=" __DATE__ " " __TIME__ "\n"); log.flush();
    QFile executable(QCoreApplication::applicationFilePath());
    if (executable.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (hash.addData(&executable)) { log.write("exe-sha256=" + hash.result().toHex() + '\n'); log.flush(); }
    }
    return true;
}
void event(const char *stage, qint64 count)
{
    appendRecord(QByteArray(stage) + (count >= 0 ? ' ' + QByteArray::number(count) : QByteArray{}));
}
void finish()
{
    if (!sessionActive) return;
    event("session-clean-exit");
    const QScopedValueRollback<bool> guard(writingDiagnostic, true);
    const QMutexLocker lock(&mutex);
    log.close();
    if (!marker.isEmpty()) QFile::remove(marker);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(previousFilter);
    if (previousAbort != SIG_ERR) std::signal(SIGABRT, previousAbort);
    previousAbort = SIG_DFL;
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportHook2(_CRT_RPTHOOK_REMOVE, crtReport);
    _CrtSetReportHookW2(_CRT_RPTHOOK_REMOVE, crtReportWide);
#endif
    if (fatalFile != INVALID_HANDLE_VALUE) CloseHandle(fatalFile);
    fatalFile = INVALID_HANDLE_VALUE;
#endif
    qInstallMessageHandler(previousMessage); previousMessage = nullptr;
    if (previousTerminate) std::set_terminate(previousTerminate);
    previousTerminate = nullptr;
    sessionActive = false;
}
}
