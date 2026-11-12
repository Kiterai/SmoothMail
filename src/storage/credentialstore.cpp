#include "credentialstore.h"
#include <qt6keychain/keychain.h>

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

void CredentialStore::read(const QString &key, Completion completion)
{
    auto *job = new QKeychain::ReadPasswordJob(m_service, this);
    job->setInsecureFallback(false);
    job->setKey(key);
    connect(job, &QKeychain::Job::finished, this, [job, completion](QKeychain::Job *) {
        completion(job->error() == QKeychain::NoError ? job->textData() : QString{},
                   jobError(job));
    });
    job->start();
}

void CredentialStore::write(const QString &key, const QString &secret, Completion completion)
{
    auto *job = new QKeychain::WritePasswordJob(m_service, this);
    job->setInsecureFallback(false);
    job->setKey(key);
    job->setTextData(secret);
    connect(job, &QKeychain::Job::finished, this, [job, completion](QKeychain::Job *) {
        completion({}, jobError(job));
    });
    job->start();
}

void CredentialStore::remove(const QString &key, Completion completion)
{
    auto *job = new QKeychain::DeletePasswordJob(m_service, this);
    job->setInsecureFallback(false);
    job->setKey(key);
    connect(job, &QKeychain::Job::finished, this, [job, completion](QKeychain::Job *) {
        completion({}, job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound
                           ? QString{} : jobError(job));
    });
    job->start();
}
} // namespace smoothmail
