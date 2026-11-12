#pragma once

#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

// Synthetic loopback server for protocol tests, not a production mail server.
class MailTestServer final : public QObject
{
public:
    enum class Protocol { Imap, Smtp };

    explicit MailTestServer(Protocol protocol, const QStringList &mechanisms, QObject *parent = nullptr)
        : QObject(parent), m_protocol(protocol), m_mechanisms(mechanisms)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            m_socket = m_server.nextPendingConnection();
            m_socket->write(m_protocol == Protocol::Imap ? "* OK localhost ready\r\n"
                                                         : "220 localhost ESMTP\r\n");
            connect(m_socket, &QTcpSocket::readyRead, this, [this] {
                while (m_socket && m_socket->canReadLine()) {
                    const auto line = m_socket->readLine(1024 * 1024).trimmed();
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
            for (const auto &mechanism : m_mechanisms) {
                capabilities += " AUTH=" + mechanism.toLatin1();
            }
            write(capabilities);
            write(m_tag + " OK capabilities");
        } else if (command == "EHLO") {
            write("250-localhost");
            write("250-AUTH " + m_mechanisms.join(u' ').toLatin1());
            write("250 SIZE 102400");
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
            if (fields.size() > mechanismIndex + 1) {
                authenticationResponses.append(QByteArray::fromBase64(fields.at(mechanismIndex + 1)));
                success();
            } else if (mechanism == "LOGIN") {
                m_waitingFor = 1;
                write(m_protocol == Protocol::Imap ? "+ VXNlcm5hbWU6" : "334 VXNlcm5hbWU6");
            } else {
                m_waitingFor = 2;
                write(m_protocol == Protocol::Imap ? "+ " : "334 ");
            }
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
    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_tag;
    int m_waitingFor = 0;
};
