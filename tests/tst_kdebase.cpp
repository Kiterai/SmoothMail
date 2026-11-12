#include <KLocalizedString>
#include <KService>
#include <QtTest>

class KdeBaseTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Test fallback text deterministically, regardless of the developer's locale.
        KLocalizedString::setApplicationDomain("smoothmail-dependency-test");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
    }

    void canFormatLocalizedText()
    {
        QCOMPARE(i18n("Mail for %1", QStringLiteral("test@example.com")),
                 QStringLiteral("Mail for test@example.com"));
        QCOMPARE(i18np("%1 item", "%1 items", 1), QStringLiteral("1 item"));
        QCOMPARE(i18np("%1 item", "%1 items", 2), QStringLiteral("2 items"));
    }

    void canConstructServiceMetadataWithoutExecuting()
    {
        // No desktop database lookup, process execution or registration.
        const KService service(QStringLiteral("SmoothMail dependency test"),
                               QStringLiteral("not-executed-command"), QStringLiteral("mail"));
        QCOMPARE(service.name(), QStringLiteral("SmoothMail dependency test"));
        QCOMPARE(service.exec(), QStringLiteral("not-executed-command"));
    }
};

QTEST_GUILESS_MAIN(KdeBaseTest)
#include "tst_kdebase.moc"
