#pragma once
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <QJsonObject>

namespace perf {
struct Counter { long long calls = 0, ns = 0; };
inline std::mutex mutex;
inline std::map<std::string, Counter> counters;
struct Scope {
    const char *name;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit Scope(const char *value) : name(value) {}
    ~Scope() {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::lock_guard lock(mutex);
        auto &counter = counters[name]; ++counter.calls; counter.ns += elapsed;
    }
};
inline void reset() { std::lock_guard lock(mutex); counters.clear(); }
inline QJsonObject snapshot() {
    std::lock_guard lock(mutex); QJsonObject result;
    for (const auto &[name, value] : counters) result[QString::fromStdString(name)] =
        QJsonObject{{"calls", double(value.calls)}, {"ms", value.ns / 1e6}};
    return result;
}
}
