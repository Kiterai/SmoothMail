#include "mime/mimeparser.h"

#include <QtTest>

using smoothmail::MimeParser;

class MimeParserTest final : public QObject
{
    Q_OBJECT

private slots:
    void decodesUtf8AndHeaders()
    {
        const QByteArray raw =
            "From: =?UTF-8?B?5pel5pys6Kqe?= <sender@example.com>\r\n"
            "To: first@example.com, Second <second@example.com>\r\n"
            "Subject: =?UTF-8?B?5pel5pys6Kqe?=\r\n"
            "Message-ID: <child@example.com>\r\n"
            "In-Reply-To: <parent@example.com>\r\n"
            "References: <root@example.com> <parent@example.com>\r\n"
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "Content-Transfer-Encoding: base64\r\n\r\n5pel5pys6Kqe\r\n";
        const auto result = MimeParser::parse(raw);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.message.rawSource, raw);
        QCOMPARE(result.message.subject, QStringLiteral("日本語"));
        QCOMPARE(result.message.senderName, QStringLiteral("日本語"));
        QCOMPARE(result.message.senderAddress, QStringLiteral("sender@example.com"));
        QCOMPARE(result.message.recipients,
                 QStringList({QStringLiteral("first@example.com"), QStringLiteral("second@example.com")}));
        QCOMPARE(result.message.messageId, QByteArray("child@example.com"));
        QCOMPARE(result.message.inReplyTo, QList<QByteArray>({"parent@example.com"}));
        QCOMPARE(result.message.references, QList<QByteArray>({"root@example.com", "parent@example.com"}));
        QCOMPARE(result.message.parts.size(), 1);
        QCOMPARE(result.message.parts.first().text, QStringLiteral("日本語"));
        QVERIFY(result.message.headers.contains(QStringLiteral("Message-ID: <child@example.com>")));
    }

    void decodesJapaneseCharsets_data()
    {
        QTest::addColumn<QByteArray>("charset");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("iso2022jp") << QByteArray("ISO-2022-JP")
                                  << QByteArray::fromHex("1b2442467c4b5c386c1b2842");
        QTest::newRow("shift-jis") << QByteArray("Shift_JIS")
                                  << QByteArray::fromHex("93fa967b8cea");
        QTest::newRow("cp932") << QByteArray("CP932")
                              << QByteArray::fromHex("93fa967b8cea");
    }

    void decodesJapaneseCharsets()
    {
        QFETCH(QByteArray, charset);
        QFETCH(QByteArray, body);
        const auto raw = "Subject: =?" + charset + "?B?" + body.toBase64()
            + "?=\r\nContent-Type: text/plain; charset=" + charset
            + "\r\nContent-Transfer-Encoding: base64\r\n\r\n" + body.toBase64() + "\r\n";
        const auto result = MimeParser::parse(raw);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.message.subject, QStringLiteral("日本語"));
        QCOMPARE(result.message.parts.size(), 1);
        QCOMPARE(result.message.parts.first().text, QStringLiteral("日本語"));
    }

    void keepsBothAlternativePartsAsSource()
    {
        const auto result = MimeParser::parse(
            "MIME-Version: 1.0\r\nContent-Type: multipart/alternative; boundary=parts\r\n\r\n"
            "--parts\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\nplain\r\n"
            "--parts\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"
            "<p>HTML</p><img src=\"https://example.com/track\">\r\n--parts--\r\n");
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.message.parts.size(), 2);
        QCOMPARE(result.message.parts.at(0).text, QStringLiteral("plain"));
        QCOMPARE(result.message.parts.at(1).mediaType, QStringLiteral("text/html"));
        QCOMPARE(result.message.parts.at(1).text,
                 QStringLiteral("<p>HTML</p><img src=\"https://example.com/track\">"));
    }

    void separatesAttachments()
    {
        const auto result = MimeParser::parse(
            "MIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=parts\r\n\r\n"
            "--parts\r\nContent-Type: text/plain\r\n\r\nbody\r\n"
            "--parts\r\nContent-Type: text/plain\r\n"
            "Content-Disposition: attachment; filename*=UTF-8''%E6%97%A5%E6%9C%AC%E8%AA%9E.txt\r\n"
            "Content-Transfer-Encoding: base64\r\n\r\naGVsbG8=\r\n--parts--\r\n");
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.message.parts.size(), 1);
        QCOMPARE(result.message.parts.first().text, QStringLiteral("body"));
        QCOMPARE(result.message.attachments.size(), 1);
        QCOMPARE(result.message.attachments.first().partId, QStringLiteral("1.2"));
        QCOMPARE(result.message.attachments.first().fileName, QStringLiteral("日本語.txt"));
    }

    void toleratesMissingOptionalHeaders()
    {
        const auto result = MimeParser::parse("Content-Type: text/plain\n\nbody");
        QVERIFY(result.ok());
        QVERIFY(result.message.subject.isEmpty());
        QVERIFY(result.message.senderAddress.isEmpty());
        QVERIFY(result.message.messageId.isEmpty());
        QCOMPARE(result.message.parts.first().text, QStringLiteral("body"));
    }

    void decodesQuotedPrintable()
    {
        const auto result = MimeParser::parse(
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "Content-Transfer-Encoding: quoted-printable\r\n\r\n"
            "=E6=97=A5=E6=9C=AC=E8=AA=9E\r\n");
        QVERIFY(result.ok());
        QCOMPARE(result.message.parts.first().text, QStringLiteral("日本語"));
    }

    void rejectsEmptyInput()
    {
        const auto result = MimeParser::parse({});
        QVERIFY(!result.ok());
        QVERIFY(!result.error.isEmpty());
    }

    void rejectsTooManyPartsWithoutPartialResult()
    {
        QByteArray raw("Content-Type: multipart/mixed; boundary=parts\r\n\r\n");
        for (int index = 0; index <= MimeParser::MaxParts; ++index) {
            raw += "--parts\r\nContent-Type: text/plain\r\n\r\nbody\r\n";
        }
        raw += "--parts--\r\n";
        const auto result = MimeParser::parse(raw);
        QVERIFY(!result.ok());
        QVERIFY(result.message.parts.isEmpty());
        QVERIFY(result.message.rawSource.isEmpty());
    }

    void rejectsDeepExtractionWithoutPartialResult()
    {
        QByteArray raw("Content-Type: text/plain\r\n\r\nbody\r\n");
        for (int index = 0; index < MimeParser::MaxPartDepth + 2; ++index) {
            const auto boundary = "part" + QByteArray::number(index);
            raw = "Content-Type: multipart/mixed; boundary=" + boundary + "\r\n\r\n--"
                + boundary + "\r\n" + raw + "\r\n--" + boundary + "--\r\n";
        }
        const auto result = MimeParser::parse(raw);
        QVERIFY(!result.ok());
        QVERIFY(result.message.parts.isEmpty());
    }

    void doesNotTreatBinaryAsBodyText()
    {
        const auto result = MimeParser::parse(
            "Content-Type: application/octet-stream\r\n"
            "Content-Transfer-Encoding: base64\r\n\r\nAAECAw==\r\n");
        QVERIFY(result.ok());
        QVERIFY(result.message.parts.isEmpty());
        QCOMPARE(result.message.attachments.size(), 1);
        QCOMPARE(result.message.attachments.first().mediaType, QStringLiteral("application/octet-stream"));
    }

    void rejectsOversizedInput()
    {
        const auto result = MimeParser::parse(QByteArray(MimeParser::MaxRawBytes + 1, 'x'));
        QVERIFY(!result.ok());
        QVERIFY(result.message.rawSource.isEmpty());
    }
};

QTEST_GUILESS_MAIN(MimeParserTest)
#include "tst_mimeparser.moc"
