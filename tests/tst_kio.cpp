#include <KIO/Global>
#include <KSslErrorUiData>
#include <QSslSocket>
#include <QtTest>

class KioTest final : public QObject
{
    Q_OBJECT

private slots:
    void faviconLookupIsDisabled()
    {
        for (const auto &text : {QStringLiteral("https://example.test/"),
                                 QStringLiteral("http://example.test/path"),
                                 QStringLiteral("file:///unused"),
                                 QStringLiteral("https://日本語.test/"), QString{}}) {
            QVERIFY(KIO::favIconForUrl(QUrl(text)).isEmpty());
        }
    }

    void sslErrorDataAbiRemainsUsable()
    {
        // This is the KIOCore API used by KIMAP/KSMTP. No connection is made,
        // and no certificate error is accepted or ignored in this smoke test.
        QSslSocket socket;
        const KSslErrorUiData data(&socket);
        KSslErrorUiData copied(data);
        KSslErrorUiData assigned;
        assigned = copied;
    }
};

QTEST_GUILESS_MAIN(KioTest)
#include "tst_kio.moc"
