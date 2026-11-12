#ifdef _WIN32
#include <winsock2.h>
#endif

#include <QDir>
#include <QtTest>

extern "C" {
#include <sasl/sasl.h>
}

namespace {

const sasl_callback_t callbacks[] = {{SASL_CB_USER, nullptr, nullptr},
                                     {SASL_CB_AUTHNAME, nullptr, nullptr},
                                     {SASL_CB_PASS, nullptr, nullptr},
                                     {SASL_CB_LIST_END, nullptr, nullptr}};

struct Connection
{
    sasl_conn_t *value = nullptr;
    ~Connection() { sasl_dispose(&value); }
};

bool answerPrompts(sasl_interact_t *interaction, const QByteArray &user, const QByteArray &secret)
{
    if (!interaction) {
        return false;
    }
    for (auto *prompt = interaction; prompt->id != SASL_CB_LIST_END; ++prompt) {
        if (prompt->id == SASL_CB_AUTHNAME || prompt->id == SASL_CB_USER) {
            prompt->result = user.constData();
            prompt->len = static_cast<unsigned>(user.size());
        } else if (prompt->id == SASL_CB_PASS) {
            prompt->result = secret.constData();
            prompt->len = static_cast<unsigned>(secret.size());
        } else {
            return false;
        }
    }
    return true;
}

int start(Connection &connection, const char *mechanism, const QByteArray &user,
          const QByteArray &secret, QByteArray &response)
{
    sasl_interact_t *interaction = nullptr;
    const char *output = nullptr;
    unsigned outputLength = 0;
    const char *selected = nullptr;
    int result;
    // A bounded loop also catches a broken plugin repeatedly requesting input.
    for (int attempt = 0; attempt < 5; ++attempt) {
        result = sasl_client_start(connection.value, mechanism, &interaction,
                                   &output, &outputLength, &selected);
        if (result != SASL_INTERACT) {
            response = QByteArray(output, outputLength);
            return result;
        }
        if (!answerPrompts(interaction, user, secret)) {
            return SASL_BADPARAM;
        }
    }
    return SASL_FAIL;
}

int step(Connection &connection, const QByteArray &challenge, const QByteArray &user,
         const QByteArray &secret, QByteArray &response)
{
    sasl_interact_t *interaction = nullptr;
    const char *output = nullptr;
    unsigned length = 0;
    for (int attempt = 0; attempt < 5; ++attempt) {
        const int result = sasl_client_step(connection.value, challenge.constData(),
                                             static_cast<unsigned>(challenge.size()),
                                             &interaction, &output, &length);
        if (result != SASL_INTERACT) {
            response = QByteArray(output, length);
            return result;
        }
        if (!answerPrompts(interaction, user, secret)) {
            return SASL_BADPARAM;
        }
    }
    return SASL_FAIL;
}

} // namespace

class SaslTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        WSADATA data;
        QCOMPARE(WSAStartup(MAKEWORD(2, 2), &data), 0);
#endif
        const auto path = qEnvironmentVariable("SMOOTHMAIL_TEST_SASL_PATH");
        QVERIFY2(!path.isEmpty(), "Set SMOOTHMAIL_TEST_SASL_PATH to the local plugin directory.");
        QVERIFY(QDir(path).exists());
        auto native = QDir::toNativeSeparators(path).toUtf8();
        QCOMPARE(sasl_set_path(SASL_PATH_TYPE_PLUGIN, native.data()), SASL_OK);
        QCOMPARE(sasl_client_init(nullptr), SASL_OK);
    }

    void mechanismsAreLoadable()
    {
        Connection connection;
        QCOMPARE(sasl_client_new("smtp", "localhost", nullptr, nullptr, callbacks, 0,
                                 &connection.value), SASL_OK);
        const char *list = nullptr;
        unsigned length = 0;
        int count = 0;
        QCOMPARE(sasl_listmech(connection.value, nullptr, "", " ", "", &list, &length, &count), SASL_OK);
        const auto mechanisms = QByteArray(list, length).split(' ');
        QVERIFY(mechanisms.contains("PLAIN"));
        QVERIFY(mechanisms.contains("LOGIN"));
        QVERIFY(mechanisms.contains("XOAUTH2"));
    }

    void plainUsesExpectedCredentials()
    {
        Connection connection;
        QCOMPARE(sasl_client_new("imap", "localhost", nullptr, nullptr, callbacks, 0,
                                 &connection.value), SASL_OK);
        QByteArray output;
        QCOMPARE(start(connection, "PLAIN", "user@example.com", "dummy-password", output), SASL_OK);
        // PLAIN may include the same authentication and authorization identity.
        const auto fields = output.split('\0');
        QCOMPARE(fields.size(), 3);
        QCOMPARE(fields.at(1), QByteArray("user@example.com"));
        QCOMPARE(fields.at(2), QByteArray("dummy-password"));
    }

    void loginCanAnswerChallenge()
    {
        Connection connection;
        QCOMPARE(sasl_client_new("smtp", "localhost", nullptr, nullptr, callbacks, 0,
                                 &connection.value), SASL_OK);
        QByteArray output;
        const QByteArray user("user@example.com");
        const QByteArray password("dummy-password");
        QCOMPARE(start(connection, "LOGIN", user, password, output), SASL_CONTINUE);
        if (output.isEmpty()) {
            QCOMPARE(step(connection, "Username:", user, password, output), SASL_CONTINUE);
            QCOMPARE(output, user);
        } else {
            QCOMPARE(output, user);
        }
        const int result = step(connection, "Password:", user, password, output);
        QVERIFY(result == SASL_OK || result == SASL_CONTINUE);
        QCOMPARE(output, password);
    }

    void xoauth2FormatsRawAccessToken()
    {
        Connection connection;
        QCOMPARE(sasl_client_new("imap", "localhost", nullptr, nullptr, callbacks, 0,
                                 &connection.value), SASL_OK);
        QByteArray output;
        QCOMPARE(start(connection, "XOAUTH2", "user@example.com", "dummy-access-token", output), SASL_CONTINUE);
        QCOMPARE(output, QByteArray("user=user@example.com\1auth=Bearer dummy-access-token\1\1"));
        sasl_interact_t *interaction = nullptr;
        const char *response = nullptr;
        unsigned responseLength = 0;
        QCOMPARE(sasl_client_step(connection.value, "", 0, &interaction, &response, &responseLength), SASL_OK);
        QCOMPARE(responseLength, 0U);
    }

    void rejectsUnknownMechanism()
    {
        Connection connection;
        QCOMPARE(sasl_client_new("smtp", "localhost", nullptr, nullptr, callbacks, 0,
                                 &connection.value), SASL_OK);
        QByteArray output;
        QCOMPARE(start(connection, "NOT-INSTALLED", "user@example.com", "dummy", output), SASL_NOMECH);
        QVERIFY(output.isEmpty());
    }

    void cleanupTestCase()
    {
        sasl_done();
#ifdef Q_OS_WIN
        WSACleanup();
#endif
    }
};

QTEST_GUILESS_MAIN(SaslTest)
#include "tst_sasl.moc"
