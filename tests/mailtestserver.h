#pragma once

#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslSocket>
#include <QSslKey>
#include <QSslCertificate>
#include <QMap>

class MailTestTcpServer final : public QTcpServer
{
protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
        addPendingConnection(socket);
    }
};

// Synthetic loopback server for protocol tests, not a production mail server.
class MailTestServer final : public QObject
{
public:
    enum class Protocol { Imap, Smtp };
    enum class AuthenticationReply { Accept, Reject, Malformed };
    enum class TlsMode { None, Implicit, StartTls };

    explicit MailTestServer(Protocol protocol, const QStringList &mechanisms, QObject *parent = nullptr)
        : QObject(parent), m_protocol(protocol), m_mechanisms(mechanisms)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            m_socket = qobject_cast<QSslSocket *>(m_server.nextPendingConnection());
            m_socket->setLocalCertificate(certificate);
            m_socket->setPrivateKey(privateKey);
            m_socket->setPeerVerifyMode(QSslSocket::VerifyNone); // Test server does not require a client certificate.
            m_socket->setProtocol(QSsl::TlsV1_2OrLater);
            if (tlsMode == TlsMode::Implicit) {
                connect(m_socket, &QSslSocket::encrypted, this, [this] { greeting(); });
                m_socket->startServerEncryption();
            } else {
                greeting();
            }
            connect(m_socket, &QTcpSocket::readyRead, this, [this] {
                while (m_socket && m_socket->bytesAvailable()) {
                    if (m_appendRemaining >= 0) {
                        const auto bytes = m_socket->read(qMin(m_appendRemaining, m_socket->bytesAvailable()));
                        m_payload += bytes;
                        m_appendRemaining -= bytes.size();
                        if (!m_appendRemaining) {
                            savedDrafts.append(m_payload); m_payload.clear(); m_appendRemaining = -1;
                            write(m_tag + " OK appended");
                        }
                        continue;
                    }
                    if (!m_socket->canReadLine()) break;
                    auto wire = m_socket->readLine(1024 * 1024);
                    if (m_receivingData) {
                        if (wire == ".\r\n") {
                            deliveredMessages.append(m_payload); m_payload.clear(); m_receivingData = false;
                            write("250 message accepted");
                        } else {
                            if (wire.startsWith("..")) wire.remove(0, 1);
                            m_payload += wire;
                        }
                        continue;
                    }
                    const auto line = wire.trimmed();
                    if (line.isEmpty()) continue;
                    commands.append(line);
                    handle(line);
                }
            });
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }

    QList<QByteArray> commands;
    QList<QByteArray> authenticationResponses;
    AuthenticationReply authenticationReply = AuthenticationReply::Accept;
    QByteArray imapEmptyChallenge = "+ ";
    TlsMode tlsMode = TlsMode::None;
    QSslCertificate certificate;
    QSslKey privateKey;
    bool provideMailbox = false;
    qint64 uidValidity = 10;
    QMap<qint64, QByteArray> messages;
    bool provideDraftFolder = false;
    QList<QByteArray> deliveredMessages;
    QList<QByteArray> savedDrafts;
    QList<QByteArray> envelopeRecipients;

    bool receivedAuthentication() const
    {
        for (const auto &line : commands) {
            if (line.startsWith("AUTH ") || line.contains(" AUTHENTICATE ") || line.contains(" LOGIN ")) {
                return true;
            }
        }
        return false;
    }

private:
    void greeting()
    {
        m_socket->write(m_protocol == Protocol::Imap ? "* OK localhost ready\r\n" : "220 localhost ESMTP\r\n");
    }
    void write(const QByteArray &line) { m_socket->write(line + "\r\n"); }

    void success()
    {
        write(m_protocol == Protocol::Imap ? m_tag + " OK authenticated" : QByteArray("235 authenticated"));
        m_waitingFor = 0;
    }

    void handle(const QByteArray &line)
    {
        if (m_waitingFor) {
            authenticationResponses.append(QByteArray::fromBase64(line));
            if (m_waitingFor == 1) {
                m_waitingFor = 2;
                write(m_protocol == Protocol::Imap ? "+ UGFzc3dvcmQ6" : "334 UGFzc3dvcmQ6");
            } else {
                success();
            }
            return;
        }

        const auto fields = line.split(' ');
        const int commandIndex = m_protocol == Protocol::Imap ? 1 : 0;
        if (fields.size() <= commandIndex) {
            write("500 malformed test request");
            return;
        }
        if (m_protocol == Protocol::Imap) {
            m_tag = fields.first();
        }
        const auto command = fields.at(commandIndex).toUpper();
        if (command == "CAPABILITY") {
            QByteArray capabilities("* CAPABILITY IMAP4rev1");
            if (tlsMode == TlsMode::StartTls && !m_socket->isEncrypted()) capabilities += " STARTTLS";
            for (const auto &mechanism : m_mechanisms) {
                capabilities += " AUTH=" + mechanism.toLatin1();
            }
            write(capabilities);
            write(m_tag + " OK capabilities");
        } else if (command == "EHLO") {
            write("250-localhost");
            if (tlsMode == TlsMode::StartTls && !m_socket->isEncrypted()) write("250-STARTTLS");
            write("250-AUTH " + m_mechanisms.join(u' ').toLatin1());
            write("250 SIZE 102400");
        } else if (command == "STARTTLS" && tlsMode == TlsMode::StartTls && !m_socket->isEncrypted()) {
            write(m_protocol == Protocol::Imap ? m_tag + " OK begin TLS" : QByteArray("220 begin TLS"));
            m_socket->flush();
            m_socket->startServerEncryption();
        } else if (command == "AUTH" || command == "AUTHENTICATE") {
            const int mechanismIndex = commandIndex + 1;
            if (fields.size() <= mechanismIndex) {
                write("500 missing mechanism");
                return;
            }
            const auto mechanism = fields.at(mechanismIndex);
            if (!m_mechanisms.contains(QString::fromLatin1(mechanism))) {
                write(m_protocol == Protocol::Imap ? m_tag + " NO unsupported mechanism" : QByteArray("504 unsupported mechanism"));
                return;
            }
            if (m_protocol == Protocol::Imap && authenticationReply != AuthenticationReply::Accept) {
                // "*" lacks the required untagged response type. It must not
                // be mistaken for a successful login (upstream error()==0 bug).
                write(authenticationReply == AuthenticationReply::Malformed ? QByteArray("*")
                                                                            : m_tag + " NO rejected test credentials");
                return;
            }
            if (fields.size() > mechanismIndex + 1) {
                authenticationResponses.append(QByteArray::fromBase64(fields.at(mechanismIndex + 1)));
                success();
            } else if (mechanism == "LOGIN") {
                m_waitingFor = 1;
                write(m_protocol == Protocol::Imap ? "+ VXNlcm5hbWU6" : "334 VXNlcm5hbWU6");
            } else {
                m_waitingFor = 2;
                write(m_protocol == Protocol::Imap ? imapEmptyChallenge : QByteArray("334 "));
            }
        } else if (provideMailbox && command == "LIST") {
            write("* LIST (\\HasNoChildren) \"/\" \"INBOX\"");
            if (provideDraftFolder) write("* LIST (\\Drafts) \"/\" \"Drafts\"");
            write(m_tag + " OK list completed");
        } else if (provideMailbox && command == "STATUS") {
            write("* STATUS \"INBOX\" (UNSEEN " + QByteArray::number(messages.size()) + ')');
            write(m_tag + " OK status completed");
        } else if (provideMailbox && (command == "EXAMINE" || command == "SELECT")) {
            write("* FLAGS (\\Seen \\Flagged)");
            write("* " + QByteArray::number(messages.size()) + " EXISTS");
            write("* OK [UIDVALIDITY " + QByteArray::number(uidValidity) + "] stable UIDs");
            write(m_tag + " OK [READ-ONLY] selected");
        } else if (provideMailbox && command == "UID" && fields.value(commandIndex + 1) == "FETCH") {
            const auto requested = fields.value(commandIndex + 2);
            int sequence = 0;
            for (auto item = messages.cbegin(); item != messages.cend(); ++item) {
                ++sequence;
                if (requested != "1:*" && requested.toLongLong() != item.key()) continue;
                const bool headersOnly = line.contains("HEADER");
                const auto raw = headersOnly ? item.value().left(item.value().indexOf("\r\n\r\n") + 4) : item.value();
                write("* " + QByteArray::number(sequence) + " FETCH (UID " + QByteArray::number(item.key())
                      + " RFC822.SIZE " + QByteArray::number(item.value().size()) + " FLAGS () "
                      + (headersOnly ? "BODY[HEADER] {" : "BODY[] {") + QByteArray::number(raw.size()) + '}');
                m_socket->write(raw);
                write(")");
            }
            write(m_tag + " OK fetch completed");
        } else if (m_protocol == Protocol::Smtp && command == "MAIL") {
            write("250 sender accepted");
        } else if (m_protocol == Protocol::Smtp && command == "RCPT") {
            envelopeRecipients.append(line);
            write("250 recipient accepted");
        } else if (m_protocol == Protocol::Smtp && command == "DATA") {
            m_receivingData = true;
            write("354 send message");
        } else if (provideMailbox && command == "APPEND") {
            auto literal = fields.last();
            literal.remove(0, 1); literal.chop(1);
            if (literal.endsWith('+')) literal.chop(1);
            bool ok = false;
            m_appendRemaining = literal.toLongLong(&ok);
            if (!ok || m_appendRemaining <= 0) { m_appendRemaining = -1; write(m_tag + " BAD invalid literal"); }
            else write("+ ready for literal");
        } else if (command == "QUIT" || command == "LOGOUT") {
            if (m_protocol == Protocol::Imap) {
                write("* BYE localhost");
                write(m_tag + " OK logout");
            } else {
                write("221 goodbye");
            }
            m_socket->disconnectFromHost();
        } else {
            write(m_protocol == Protocol::Imap ? m_tag + " BAD unsupported test command" : QByteArray("500 unsupported test command"));
        }
    }

    Protocol m_protocol;
    QStringList m_mechanisms;
    MailTestTcpServer m_server;
    QPointer<QSslSocket> m_socket;
    QByteArray m_tag;
    int m_waitingFor = 0;
    bool m_receivingData = false;
    qint64 m_appendRemaining = -1;
    QByteArray m_payload;
};
