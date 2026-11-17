#pragma once

#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslSocket>
#include <QSslKey>
#include <QSslCertificate>
#include <QMap>
#include <QSet>
#include <limits>
#include <QDateTime>
#include <QLocale>
#include <QTimer>

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
            m_pausedMetadata.clear();
            m_socket = qobject_cast<QSslSocket *>(m_server.nextPendingConnection());
            m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            m_socket->setLocalCertificate(certificate);
            m_socket->setPrivateKey(privateKey);
            m_socket->setPeerVerifyMode(QSslSocket::VerifyNone); // Test server does not require a client certificate.
            m_socket->setProtocol(QSsl::TlsV1_2OrLater);
            connect(m_socket, &QSslSocket::encrypted, this, [this] { tlsEstablished = true; });
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
    bool tlsEstablished = false;
    bool changeMechanismsAfterTls = false;
    QStringList postTlsMechanisms;
    bool provideMailbox = false;
    bool duplicateInboxListing = false;
    QList<QByteArray> listResponses, listTailResponses;
    int listTailDelayMs = 0;
    QSet<QByteArray> extraFolders;
    bool rejectFolderChanges = false;
    qint64 advertisedMessageCount = -1;
    qint64 advertisedMessageSize = -1;
    bool disconnectOnBodyFetch = false;
    QMap<qint64, QList<QByteArray>> messageFlags;
    bool rejectFlagChanges = false;
    int fullFetchCount = 0;
    int headerFetchCount = 0, flagsFetchCount = 0, maximumFlagsWindow = 0;
    qint64 omitFlagUid = -1;
    bool rejectFlagSearch = false;
    bool malformFlagSearchReply = false;
    qint64 extraFlagSearchUid = 0;
    int maximumHeaderWindow = 0;
    qint64 uidValidity = 10;
    QMap<qint64, QByteArray> messages;
    QMap<QByteArray, QMap<qint64, QByteArray>> folderMessages;
    QMap<QByteArray, QMap<qint64, QList<QByteArray>>> folderFlags;
    bool ignoreFlagWrites = false;
    bool pauseMetadata = false;
    bool supportMove = true, omitMoveMapping = false;
    bool supportListStatus = false, rejectListStatus = false;
    QByteArray omitListStatusFolder;
    QMap<QByteArray, qint64> uidNextFloor;
    int pausedMetadataCount() const { return m_pausedMetadata.size(); }
    void resumeMetadata() { pauseMetadata = false; const auto pending = m_pausedMetadata; m_pausedMetadata.clear(); for (const auto &line : pending) if (m_socket && m_socket->isOpen()) handle(line); }
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
    QByteArray m_selected = "INBOX";
    QList<QByteArray> m_pausedMetadata;
    QMap<qint64, QByteArray> &selectedMessages() { return m_selected == "INBOX" ? messages : folderMessages[m_selected]; }
    QMap<qint64, QList<QByteArray>> &selectedFlags() { return m_selected == "INBOX" ? messageFlags : folderFlags[m_selected]; }
    static bool includesUid(const QByteArray &set, qint64 uid)
    {
        for (const auto &part : set.split(',')) {
            const auto ends = part.split(':');
            const auto high = ends.size() > 1 ? (ends.last() == "*" ? std::numeric_limits<qint64>::max() : ends.last().toLongLong()) : ends.first().toLongLong();
            if (uid >= ends.first().toLongLong() && uid <= high) return true;
        }
        return false;
    }
    QStringList advertisedMechanisms() const
    {
        return changeMechanismsAfterTls && m_socket && m_socket->isEncrypted() ? postTlsMechanisms : m_mechanisms;
    }
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
            if (supportMove) capabilities += " MOVE UIDPLUS";
            if (supportListStatus) capabilities += " LIST-EXTENDED LIST-STATUS";
            if (tlsMode == TlsMode::StartTls && !m_socket->isEncrypted()) capabilities += " STARTTLS";
            for (const auto &mechanism : advertisedMechanisms()) {
                capabilities += " AUTH=" + mechanism.toLatin1();
            }
            write(capabilities);
            write(m_tag + " OK capabilities");
        } else if (command == "EHLO") {
            write("250-localhost");
            if (tlsMode == TlsMode::StartTls && !m_socket->isEncrypted()) write("250-STARTTLS");
            const auto mechanisms = advertisedMechanisms();
            if (!mechanisms.isEmpty()) write("250-AUTH " + mechanisms.join(u' ').toLatin1());
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
            if (!advertisedMechanisms().contains(QString::fromLatin1(mechanism))) {
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
            const bool extended = line.contains("RETURN (STATUS");
            if (extended && rejectListStatus) { write(m_tag + " NO rejected LIST STATUS"); return; }
            const auto status = [&](const QByteArray &name) {
                if (!extended || name == omitListStatusFolder) return;
                const auto &data = name == "INBOX" ? messages : folderMessages[name];
                const auto &flags = name == "INBOX" ? messageFlags : folderFlags[name];
                qint64 unseen = 0;
                for (auto it = data.cbegin(); it != data.cend(); ++it) if (!flags.value(it.key()).contains("\\Seen")) ++unseen;
                write("* STATUS \"" + name + "\" (UNSEEN " + QByteArray::number(unseen) + ')');
            };
            if (!listResponses.isEmpty()) { for (const auto &response : listResponses) write(response); }
            else {
                write("* LIST (\\HasNoChildren) \"/\" \"INBOX\"");
                status("INBOX");
                if (duplicateInboxListing) write("* LIST (\\HasNoChildren) \"/\" \"INBOX\"");
                if (provideDraftFolder) write("* LIST (\\Drafts) \"/\" \"Drafts\"");
                for (const auto &name : extraFolders) { write("* LIST () \"/\" \"" + name + '"'); status(name); }
            }
            const auto completeList = [socket = QPointer<QSslSocket>(m_socket), tag = m_tag, tail = listTailResponses] {
                if (!socket || !socket->isOpen()) return;
                for (const auto &response : tail) socket->write(response + "\r\n");
                socket->write(tag + " OK list completed\r\n");
            };
            if (listTailDelayMs) QTimer::singleShot(listTailDelayMs, this, completeList);
            else completeList();
        } else if (provideMailbox && (command == "CREATE" || command == "RENAME" || command == "DELETE")) {
            if (rejectFolderChanges) { write(m_tag + " NO synthetic permission denied"); return; }
            const auto names = line.split('"');
            const auto source = names.value(1);
            if (command == "CREATE") {
                if (extraFolders.contains(source)) { write(m_tag + " NO already exists"); return; }
                extraFolders.insert(source);
            } else if (command == "RENAME") {
                if (!extraFolders.contains(source) || extraFolders.contains(names.value(3))) { write(m_tag + " NO cannot rename"); return; }
                const auto old = extraFolders;
                for (const auto &name : old) if (name == source || name.startsWith(source + '/')) {
                    const auto target = names.value(3) + name.mid(source.size());
                    extraFolders.remove(name); extraFolders.insert(target);
                    folderMessages[target] = folderMessages.take(name); folderFlags[target] = folderFlags.take(name);
                }
            } else { extraFolders.remove(source); folderMessages.remove(source); folderFlags.remove(source); }
            write(m_tag + " OK changed");
        } else if (provideMailbox && command == "STATUS") {
            const auto mailbox = line.split('"').value(1);
            const auto &data = mailbox == "INBOX" ? messages : folderMessages[mailbox];
            const auto &flags = mailbox == "INBOX" ? messageFlags : folderFlags[mailbox];
            qint64 unseen = 0;
            for (auto it = data.cbegin(); it != data.cend(); ++it) if (!flags.value(it.key()).contains("\\Seen")) ++unseen;
            write("* STATUS \"" + mailbox + "\" (UNSEEN " + QByteArray::number(unseen) + ')');
            write(m_tag + " OK status completed");
        } else if (provideMailbox && (command == "EXAMINE" || command == "SELECT")) {
            m_selected = line.split('"').value(1);
            const auto &data = selectedMessages();
            write("* FLAGS (\\Seen \\Flagged)");
            write("* " + QByteArray::number(advertisedMessageCount < 0 ? data.size() : advertisedMessageCount) + " EXISTS");
            write("* OK [UIDVALIDITY " + QByteArray::number(uidValidity) + "] stable UIDs");
            auto &next = uidNextFloor[m_selected]; next = qMax(next, data.isEmpty() ? qint64(1) : data.lastKey() + 1);
            write("* OK [UIDNEXT " + QByteArray::number(next) + "] next UID");
            write(m_tag + (command == "EXAMINE" ? " OK [READ-ONLY] selected" : " OK [READ-WRITE] selected"));
        } else if (provideMailbox && command == "UID" && fields.value(commandIndex + 1) == "SEARCH") {
            const bool unseen = line.contains(" NOT SEEN "), flagged = line.contains(" FLAGGED ");
            if ((unseen || flagged) && rejectFlagSearch) { write(m_tag + " NO unsupported boolean search"); return; }
            QByteArray result("* SEARCH");
            const auto since = QLocale::c().toDate(QString::fromLatin1(fields.last()).remove(u'"'), QStringLiteral("d-MMM-yyyy"));
            for (auto it = selectedMessages().cbegin(); it != selectedMessages().cend(); ++it) {
                QDateTime date;
                for (const auto &header : it.value().split('\n')) if (header.startsWith("Date:")) date = QDateTime::fromString(QString::fromLatin1(header.mid(5)).trimmed(), Qt::RFC2822Date);
                const auto flags = selectedFlags().value(it.key());
                if ((!since.isValid() || (date.isValid() && date.date() >= since))
                    && (!unseen || !flags.contains("\\Seen")) && (!flagged || flags.contains("\\Flagged")))
                    result += ' ' + QByteArray::number(it.key());
            }
            if (flagged && extraFlagSearchUid > 0) result += ' ' + QByteArray::number(extraFlagSearchUid);
            write(result); write((unseen || flagged) && malformFlagSearchReply ? m_tag : m_tag + " OK search completed");
        } else if (provideMailbox && command == "UID" && fields.value(commandIndex + 1) == "MOVE") {
            const auto target = line.split('"').value(1); const auto uid = fields.value(commandIndex + 2).toLongLong();
            if (!supportMove || (!extraFolders.contains(target) && target != "INBOX")) { write(m_tag + " NO unsupported move target"); return; }
            auto &source = selectedMessages(); auto &destination = target == "INBOX" ? messages : folderMessages[target];
            if (!source.contains(uid)) { write(m_tag + " OK no message"); return; }
            const qint64 newUid = qMax(uidNextFloor.value(target, 1), destination.isEmpty() ? qint64(1) : destination.lastKey() + 1); uidNextFloor[target] = newUid + 1;
            destination.insert(newUid, source.take(uid)); auto &targetFlags = target == "INBOX" ? messageFlags : folderFlags[target]; targetFlags[newUid] = selectedFlags().take(uid);
            write(m_tag + (omitMoveMapping ? QByteArray(" OK moved") : " OK [COPYUID " + QByteArray::number(uidValidity) + ' ' + QByteArray::number(uid) + ' ' + QByteArray::number(newUid) + "] moved"));
        } else if (provideMailbox && command == "UID" && fields.value(commandIndex + 1) == "STORE") {
            if (rejectFlagChanges) { write(m_tag + " NO synthetic store rejected"); return; }
            const auto requested = fields.value(commandIndex + 2);
            for (auto it = selectedMessages().cbegin(); it != selectedMessages().cend(); ++it) if (includesUid(requested, it.key())) {
                const auto uid = it.key();
                const auto flag = line.contains("\\Flagged") ? QByteArray("\\Flagged") : QByteArray("\\Seen");
                auto &flags = selectedFlags()[uid];
                if (!ignoreFlagWrites) {
                    if (line.contains("+FLAGS")) { if (!flags.contains(flag)) flags.append(flag); }
                    else flags.removeAll(flag);
                }
                write("* 1 FETCH (UID " + QByteArray::number(uid) + " FLAGS (" + flags.join(' ') + "))");
            }
            write(m_tag + " OK store completed");
        } else if (provideMailbox && (command == "FETCH" || (command == "UID" && fields.value(commandIndex + 1) == "FETCH"))) {
            const bool flagsOnly = !line.contains("BODY") && !line.contains("RFC822");
            if (pauseMetadata && (line.contains("HEADER") || flagsOnly)) { m_pausedMetadata.append(line); return; }
            if (disconnectOnBodyFetch && !line.contains("HEADER") && !flagsOnly) { m_socket->disconnectFromHost(); return; }
            const bool uidBased = command == "UID";
            const auto requested = fields.value(commandIndex + (uidBased ? 2 : 1));
            const auto range = requested.split(':');
            const auto first = range.value(0).toLongLong();
            const auto last = range.size() > 1 ? (range.value(1) == "*" ? std::numeric_limits<qint64>::max() : range.value(1).toLongLong()) : first;
            QList<QPair<qint64, qint64>> intervals;
            for (const auto &interval : requested.split(',')) {
                const auto ends = interval.split(':'); const auto low = ends.value(0).toLongLong();
                const auto high = ends.size() > 1 ? (ends.value(1) == "*" ? std::numeric_limits<qint64>::max() : ends.value(1).toLongLong()) : low;
                intervals.append({low, high});
            }
            if (line.contains("HEADER")) maximumHeaderWindow = qMax(maximumHeaderWindow, int(qMin<qint64>(100000, last - first + 1)));
            int sequence = 0;
            int returnedFlags = 0;
            const auto &data = selectedMessages();
            auto begin = uidBased && !requested.contains(',') && range.size() == 1 ? data.constFind(first) : data.cbegin();
            for (auto item = begin; item != data.cend(); ++item) {
                ++sequence;
                const auto position = uidBased ? item.key() : sequence;
                bool included = false;
                for (const auto &interval : intervals) {
                    if (position >= interval.first && position <= interval.second) { included = true; break; }
                }
                if (!included) continue;
                if (flagsOnly) {
                    if (item.key() == omitFlagUid) continue;
                    ++flagsFetchCount; ++returnedFlags;
                    write("* " + QByteArray::number(sequence) + " FETCH (UID " + QByteArray::number(item.key()) + " FLAGS (" + selectedFlags().value(item.key()).join(' ') + "))");
                    continue;
                }
                const bool headersOnly = line.contains("HEADER");
                if (headersOnly) ++headerFetchCount;
                if (!headersOnly) ++fullFetchCount;
                const auto raw = headersOnly ? item.value().left(item.value().indexOf("\r\n\r\n") + 4) : item.value();
                write("* " + QByteArray::number(sequence) + " FETCH (UID " + QByteArray::number(item.key())
                      + " RFC822.SIZE " + QByteArray::number(advertisedMessageSize < 0 ? item.value().size() : advertisedMessageSize) + " FLAGS (" + selectedFlags().value(item.key()).join(' ') + ") "
                      + (headersOnly ? "BODY[HEADER] {" : "BODY[] {") + QByteArray::number(raw.size()) + '}');
                m_socket->write(raw);
                write(")");
            }
            maximumFlagsWindow = qMax(maximumFlagsWindow, returnedFlags);
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
