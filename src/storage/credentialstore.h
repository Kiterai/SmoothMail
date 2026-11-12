#pragma once

#include <QObject>
#include <functional>

namespace smoothmail {
class CredentialStore final : public QObject
{
    Q_OBJECT
public:
    using Completion = std::function<void(const QString &value, const QString &error)>;
    explicit CredentialStore(QObject *parent = nullptr, QString service = QStringLiteral("SmoothMail"));
    void read(const QString &key, Completion completion, bool allowMissing = false);
    void write(const QString &key, const QString &secret, Completion completion);
    void remove(const QString &key, Completion completion);
private:
    QString m_service;
};
} // namespace smoothmail
