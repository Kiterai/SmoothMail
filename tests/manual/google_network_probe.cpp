// Opt-in live network diagnostics. Never called from builds or CTest.
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QTimer>
#include <QUrlQuery>
#include <iostream>

namespace {
bool line(QSslSocket &socket, QByteArray &value)
{
    while (!socket.canReadLine())
        if (!socket.waitForReadyRead(15000))
            return false;
    value = socket.readLine(65536);
    return !value.isEmpty() && value.size() < 65536;
}

bool readiness(const QString &host, quint16 port, bool imap, bool startTls)
{
    QSslSocket socket;
    if (startTls)
        socket.connectToHost(host, port);
    else
        socket.connectToHostEncrypted(host, port);
    bool ok = startTls ? socket.waitForConnected(15000) : socket.waitForEncrypted(15000);
    QByteArray response;
    ok = ok && line(socket, response) && response.startsWith(imap ? "* OK" : "220");
    auto command = [&](const QByteArray &request, bool capability) {
        socket.write(request);
        if (!socket.waitForBytesWritten(15000))
            return false;
        bool advertised = false;
        for (int i = 0; i < 100; ++i) {
            if (!line(socket, response))
                return false;
            advertised |= response.toUpper().contains(imap ? "AUTH=XOAUTH2" : "XOAUTH2");
            if (imap && response.startsWith("a1 "))
                return response.startsWith("a1 OK") && (!capability || advertised);
            if (!imap && response.size() >= 4 && response[3] == ' ')
                return response.startsWith("250") && (!capability || advertised);
        }
        return false;
    };
    if (ok && startTls) {
        ok = command("EHLO smoothmail.invalid\r\n", false);
        if (ok) {
            socket.write("STARTTLS\r\n");
            ok = socket.waitForBytesWritten(15000) && line(socket, response) && response.startsWith("220");
        }
        if (ok) {
            socket.startClientEncryption();
            ok = socket.waitForEncrypted(15000);
        }
    }
    if (ok)
        ok = command(imap ? "a1 CAPABILITY\r\n" : "EHLO smoothmail.invalid\r\n", true);
    socket.disconnectFromHost();
    std::cout << (imap ? "imap-993" : startTls ? "smtp-587" : "smtp-465")
              << ": " << (ok ? "TLS_VERIFIED_XOAUTH2_ADVERTISED" : "FAILED") << '\n';
    return ok;
}

int tokenConfiguration(const QString &clientId)
{
    if (!clientId.endsWith(".apps.googleusercontent.com") || clientId.contains(QChar(0)))
        return 2;
    QUrlQuery body;
    body.addQueryItem("client_id", clientId);
    body.addQueryItem("grant_type", "authorization_code");
    body.addQueryItem("code", "smoothmail-deliberately-invalid-code");
    body.addQueryItem("code_verifier", "smoothmail-deliberately-invalid-verifier-for-configuration-check");
    body.addQueryItem("redirect_uri", "http://127.0.0.1:49152/");
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl("https://oauth2.googleapis.com/token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    auto *reply = manager.post(request, body.query(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(25000);
    loop.exec();
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 400 && status != 401) {
        std::cout << "TOKEN_CONFIGURATION_FAILED_RESPONSE_NOT_DISPLAYED\n";
        return 1;
    }
    const auto bytes = reply->read(131073);
    if (bytes.size() > 131072)
        return 1;
    const auto data = QJsonDocument::fromJson(bytes).object();
    const auto error = data["error"].toString();
    const QStringList safe{"invalid_request", "invalid_client", "invalid_grant", "unauthorized_client"};
    std::cout << "TOKEN_CONFIGURATION_ERROR=" << (safe.contains(error) ? error.toStdString() : "other")
              << "\nCLIENT_SECRET_REQUIRED="
              << (data["error_description"].toString() == "client_secret is missing." ? "true" : "not_confirmed")
              << '\n';
    return 0;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() == 2 && args[1] == "--readiness") {
        const bool imap = readiness("imap.gmail.com", 993, true, false);
        const bool smtp = readiness("smtp.gmail.com", 465, false, false);
        const bool startTls = readiness("smtp.gmail.com", 587, false, true);
        return imap && smtp && startTls ? 0 : 1;
    }
    if (args.size() == 3 && args[1] == "--token-configuration")
        return tokenConfiguration(args[2]);
    std::cerr << "Explicit --readiness or --token-configuration CLIENT_ID required; live network access.\n";
    return 2;
}
