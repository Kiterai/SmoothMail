#include "perf.h"
#include "perfserver.h"
#include "mail/mailservice.h"
#include "storage/accountrepository.h"
#include "core/diagnostics.h"
#include "ui/mainwindow.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>
#include <QElapsedTimer>
#include <QSslConfiguration>
#include <windows.h>
#include <cstdio>

using namespace smoothmail;
static double cpuMs() {
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    ULARGE_INTEGER k{}, u{}; k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return (k.QuadPart + u.QuadPart) / 10000.0;
}
static QByteArray message(int uid, int bytes, bool old = false) {
    QByteArray raw = "From: sender@example.test\r\nTo: receiver@example.test\r\nSubject: synthetic " +
        QByteArray::number(uid) + "\r\nMessage-ID: <" + QByteArray::number(uid) + "@example.test>\r\nDate: " +
        (old ? QByteArray("Sun, 01 Nov 2026 10:00:00 +0000") : QByteArray("Wed, 18 Nov 2026 10:00:00 +0000")) +
        "\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n";
    return raw + QByteArray(qMax(0, bytes - int(raw.size())), 'x');
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setQuitLockEnabled(false);
    if (qEnvironmentVariable("SMOOTHMAIL_PERF_GUEST") != "1") return 2;
    const auto args = app.arguments();
    const QString output = args.value(1);
    if (output.isEmpty() || !QDir().mkpath(output)) return 3;
    QFile results(output + "/results.jsonl");
    if (!results.open(QIODevice::WriteOnly | QIODevice::Append)) return 4;
    const bool logEnabled = !args.contains("--no-diagnostics");
    if (logEnabled && !diagnostics::start(output + "/diagnostics")) return 5;
    int failures = 0;
    auto write = [&](const QJsonObject &row) {
        results.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n'); results.flush();
    };
    auto scenario = [&](QString name, int count, int bytes, int folders, int delay, bool old, int repeat, bool tls = false) {
        MailTestServer server(MailTestServer::Protocol::Imap, {QStringLiteral("PLAIN")});
        server.provideMailbox = true; server.commandDelayMs = delay;
        if (tls) {
            QFile cert(":/test/tls-cert.pem"), key(":/test/tls-key.pem");
            if (!cert.open(QIODevice::ReadOnly) || !key.open(QIODevice::ReadOnly)) { ++failures; return; }
            server.tlsMode = MailTestServer::TlsMode::Implicit;
            server.certificate = QSslCertificate(cert.readAll(), QSsl::Pem);
            server.privateKey = QSslKey(key.readAll(), QSsl::Rsa, QSsl::Pem);
            auto config = QSslConfiguration::defaultConfiguration();
            config.addCaCertificate(server.certificate); QSslConfiguration::setDefaultConfiguration(config);
        }
        for (int i = 1; i <= count; ++i) server.messages[i] = message(i, bytes, old && i < count);
        for (int i = 1; i < folders; ++i) server.extraFolders.insert("Folder-" + QByteArray::number(i));
        if (!server.listen()) { ++failures; return; }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        Account account{id, "perf@example.test", {"127.0.0.1", 465, "test"},
            {"127.0.0.1", server.port(), "test", AuthenticationMode::Plain, tls ? EncryptionMode::ImplicitTls : EncryptionMode::None}};
        AccountRepository cache(output + "/" + id + ".sqlite");
        QEventLoop insertedLoop; QString insertError; bool inserted = false;
        QObject::connect(&cache, &AccountRepository::inserted, &insertedLoop,
            [&](const QString &) { inserted = true; insertedLoop.quit(); });
        QObject::connect(&cache, &AccountRepository::failed, &insertedLoop,
            [&](const QString &, const QString &error) { insertError = error; insertedLoop.quit(); });
        QTimer::singleShot(30000, &insertedLoop, &QEventLoop::quit);
        cache.insert(account); insertedLoop.exec();
        if (!inserted || !insertError.isEmpty()) { ++failures; return; }
        MailService service(QCoreApplication::applicationDirPath() + "/sasl2");
        auto run = [&](QString phase, MailService::SyncMode mode, QString folder = {}) {
            server.commands.clear(); server.fullFetchCount = server.headerFetchCount = server.flagsFetchCount = 0; perf::reset();
            QEventLoop loop; bool done = false; QString error;
            auto connection = QObject::connect(&service, &MailService::synchronized, &loop,
                [&](const QString &, const QString &, const QList<MailFolder> &, const QList<MailRecord> &, const QString &e) {
                    done = true; error = e; loop.quit();
                });
            QTimer::singleShot(300000, &loop, &QEventLoop::quit);
            const double cpu = cpuMs(); QElapsedTimer wall; wall.start();
            service.synchronize(account, "synthetic-only", folder, &cache, mode); loop.exec();
            const double elapsed = wall.nsecsElapsed() / 1e6, cpuElapsed = cpuMs() - cpu;
            QObject::disconnect(connection);
            QJsonObject commands;
            for (const auto &line : server.commands) {
                const auto fields = line.split(' ');
                QString key = QString::fromLatin1(fields.value(1));
                if (key == "UID") key += " " + QString::fromLatin1(fields.value(2));
                if (key.contains("FETCH")) key += line.contains("HEADER") ? " header"
                    : line.contains("BODY") || line.contains("RFC822") ? " body" : " flags";
                commands[key] = commands[key].toInt() + 1;
            }
            bool ok = done && error.isEmpty();
            const auto timings = perf::snapshot();
            double pageMs = 0, renderMs = 0;
            if (ok && args.contains("--deep-only")) {
                QEventLoop pageLoop; MailPage page; bool loaded = false;
                const auto pageConnection = QObject::connect(&cache, &AccountRepository::pageLoaded, &pageLoop,
                    [&](const MailPage &value) { page = value; loaded = true; pageLoop.quit(); });
                QTimer::singleShot(30000, &pageLoop, &QEventLoop::quit);
                QElapsedTimer pageTime; pageTime.start();
                MailQuery query; query.accountId = account.id; query.folder = "INBOX";
                cache.loadPage(query); pageLoop.exec(); pageMs = pageTime.nsecsElapsed() / 1e6;
                QObject::disconnect(pageConnection);
                ok = loaded && page.error.isEmpty() && page.total == server.messages.size();
                MainWindow window(false); window.setPagedStorage(true); window.setAccounts({account});
                MailQuery uiQuery;
                QObject::connect(&window, &MainWindow::pageRequested, &window, [&](const MailQuery &value) { uiQuery = value; });
                window.applySnapshot(account.id, "INBOX", {{"INBOX", u'/', {}, server.messages.size()}}, {}, {});
                window.show(); app.processEvents();
                page.query = uiQuery;
                QElapsedTimer render; render.start(); window.applyPage(page); window.repaint(); app.processEvents();
                renderMs = render.nsecsElapsed() / 1e6;
                if (repeat == 1 && phase == "add10-recent") window.grab().save(output + "/" + name + ".png");
            }
            write({{"scenario", name}, {"phase", phase}, {"repeat", repeat}, {"messages", server.messages.size()},
                {"bytes_per_message", bytes}, {"folders", folders}, {"delay_ms", delay}, {"diagnostics", logEnabled},
                {"wall_ms", elapsed}, {"cpu_ms", cpuElapsed}, {"ok", ok}, {"error", error},
                {"page_ms", pageMs}, {"render_ms", renderMs}, {"tls", tls},
                {"header_messages", server.headerFetchCount}, {"flags_messages", server.flagsFetchCount},
                {"flag_searches", qMax(0, commands.value("UID SEARCH").toInt() - 1)},
                {"body_messages", server.fullFetchCount}, {"commands", commands}, {"timings", timings}});
            if (!ok) ++failures;
            return ok;
        };
        if (!run("initial", MailService::SyncMode::All)) return;
        if (!run("unchanged-all", MailService::SyncMode::All)) return;
        if (!run("unchanged-recent", MailService::SyncMode::Recent)) return;
        for (int i = count + 1; i <= count + 10; ++i) server.messages[i] = message(i, bytes);
        if (!run("add10-recent", MailService::SyncMode::Recent)) return;
        run("unchanged-selected", MailService::SyncMode::SelectedRecent, "INBOX");
    };
    for (int repeat = 1; repeat <= 3; ++repeat) {
        if (args.contains("--window-only")) {
            scenario("small-1000", 1000, 2048, 1, 0, false, repeat);
            scenario("small-5000", 5000, 2048, 1, 0, false, repeat);
            scenario("delay20-1000", 1000, 2048, 1, 20, false, repeat);
            continue;
        }
        if (args.contains("--deep-only")) {
            scenario("small-5000", 5000, 2048, 1, 0, false, repeat);
            scenario("large-1000", 1000, 65536, 1, 0, false, repeat);
            scenario("historical-5000", 5000, 2048, 1, 0, true, repeat);
            scenario("tls-1000", 1000, 2048, 1, 0, false, repeat, true);
            continue;
        }
        scenario("small-100", 100, 2048, 1, 0, false, repeat);
        scenario("small-1000", 1000, 2048, 1, 0, false, repeat);
        scenario("small-5000", 5000, 2048, 1, 0, false, repeat);
        scenario("large-1000", 1000, 65536, 1, 0, false, repeat);
        scenario("historical-5000", 5000, 2048, 1, 0, true, repeat);
        scenario("folders-30", 100, 2048, 30, 0, false, repeat);
        scenario("delay20-1000", 1000, 2048, 1, 20, false, repeat);
        scenario("delay20-folders30", 100, 2048, 30, 20, false, repeat);
    }
    diagnostics::finish();
    QFile complete(output + "/complete.txt");
    if (complete.open(QIODevice::WriteOnly)) complete.write(QByteArray::number(failures));
    return failures ? 1 : 0;
}
