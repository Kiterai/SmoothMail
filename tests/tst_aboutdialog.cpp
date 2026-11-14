#include "ui/aboutdialog.h"
#include "ui/mainwindow.h"

#include <QAction>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSplitter>
#include <QTest>

class AboutDialogTest : public QObject
{
    Q_OBJECT
private slots:
    void embeddedNotices()
    {
        smoothmail::AboutDialog dialog;
        auto *items = dialog.findChild<QListWidget *>(QStringLiteral("licenseItems"));
        auto *text = dialog.findChild<QPlainTextEdit *>(QStringLiteral("licenseText"));
        QVERIFY(items && text);
        QVERIFY(text->isReadOnly());
        QVERIFY(items->count() >= 3);
        QCOMPARE(dialog.findChild<QSplitter *>()->orientation(), Qt::Horizontal);
        QVERIFY(text->toPlainText().contains(QStringLiteral("MIT License")));
        bool foundChanges = false;
        for (int row = 0; row < items->count(); ++row) {
            items->setCurrentRow(row);
            QVERIFY2(!text->toPlainText().isEmpty(), "Every license entry must have offline text");
            QVERIFY(!text->toPlainText().contains(QStringLiteral("読み込めません")));
            foundChanges |= text->toPlainText().contains(QStringLiteral("favicon"));
        }
        QVERIFY(foundChanges);
        const auto identity = dialog.findChild<QLabel *>(QStringLiteral("buildIdentity"))->text();
        QVERIFY(QRegularExpression(QStringLiteral("ビルド日 \\d{4}-\\d{2}-\\d{2} / コミット ")).match(identity).hasMatch());
        QVERIFY(!identity.contains(QStringLiteral("C:\\")));
    }
    void menuWorksWithoutAccounts()
    {
        smoothmail::MainWindow window(false);
        auto *action = window.findChild<QAction *>(QStringLiteral("aboutAction"));
        QVERIFY(action && action->isEnabled());
        action->trigger();
        auto *dialog = window.findChild<smoothmail::AboutDialog *>();
        QVERIFY(dialog);
        action->trigger();
        QCOMPARE(window.findChildren<smoothmail::AboutDialog *>().size(), 1);
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!window.findChild<smoothmail::AboutDialog *>());
    }
};
QTEST_MAIN(AboutDialogTest)
#include "tst_aboutdialog.moc"
