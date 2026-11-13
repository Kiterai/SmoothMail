#include "credentialstore.h"
#include <qt6keychain/keychain.h>
#include <QPointer>

namespace smoothmail {
namespace {
QString jobError(const QKeychain::Job *job)
{
    if (job->error() == QKeychain::NoError) return {};
    return job->errorString().isEmpty() ? QStringLiteral("OSの秘密情報ストアを利用できません。") : job->errorString();
}
}
CredentialStore::CredentialStore(QObject *parent, QString service)
    : QObject(parent), m_service(std::move(service))
{
}

void CredentialStore::read(const QString &key, Completion completion, bool allowMissing)
{
    enqueue([this, key, completion, allowMissing] {
    auto *job = new QKeychain::ReadPasswordJob(m_service, this);
    job->setInsecureFallback(false);
    job->setKey(key);
    connect(job, &QKeychain::Job::finished, this, [this, job, completion, allowMissing](QKeychain::Job *) {
        const QPointer<CredentialStore> guard(this);
        completion(job->error() == QKeychain::NoError ? job->textData() : QString{},
                   allowMissing && job->error() == QKeychain::EntryNotFound ? QString{} : jobError(job));
        if (guard) { m_running = false; advance(); }
    });
    job->start();
    });
}

void CredentialStore::write(const QString &key, const QString &secret, Completion completion)
{
    enqueue([this, key, secret, completion] {
    auto *job = new QKeychain::WritePasswordJob(m_service, this);
    job->setInsecureFallback(false);
    job->setKey(key);
    job->setTextData(secret);
    connect(job, &QKeychain::Job::finished, this, [this, job, completion](QKeychain::Job *) {
        const QPointer<CredentialStore> guard(this);
        completion({}, jobError(job));
        if (guard) { m_running = false; advance(); }
    });
    job->start();
    });
}

void CredentialStore::remove(const QString &key, Completion completion)
{
    enqueue([this, key, completion] {
    auto *job = new QKeychain::DeletePasswordJob(m_service, this);
    job->setInsecureFallback(false);
    job->setKey(key);
    connect(job, &QKeychain::Job::finished, this, [this, job, completion](QKeychain::Job *) {
        const QPointer<CredentialStore> guard(this);
        completion({}, job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound
                            ? QString{} : jobError(job));
        if (guard) { m_running = false; advance(); }
    });
    job->start();
    });
}
void CredentialStore::enqueue(std::function<void()> job) { m_jobs.enqueue(std::move(job)); advance(); }
void CredentialStore::advance()
{
    if (m_running || m_jobs.isEmpty()) return;
    m_running = true; auto job = m_jobs.dequeue(); job();
}
} // namespace smoothmail
