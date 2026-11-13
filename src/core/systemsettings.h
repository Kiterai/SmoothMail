#pragma once
#include <QString>
#include <QMetaType>
namespace smoothmail {
struct SystemSettings {
    int pageSize = 100;
    int refreshSeconds = 30;
    QString googleClientId;
    int parallelUpdates = 4;
    bool operator==(const SystemSettings &) const = default;
};
inline bool validSettings(const SystemSettings &settings) { return settings.pageSize >= 1 && settings.pageSize <= 1000 && settings.refreshSeconds >= 1 && settings.refreshSeconds <= 86400 && settings.parallelUpdates >= 1 && settings.parallelUpdates <= 32; }
}
Q_DECLARE_METATYPE(smoothmail::SystemSettings)
