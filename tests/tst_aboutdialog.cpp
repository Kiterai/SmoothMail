#include "ui/aboutdialog.h"
#include "ui/mainwindow.h"

#include <QAction>
#include <QLabel>
#include <QListWidget>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
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
        auto *text = dialog.findChild<QTextBrowser *>(QStringLiteral("licenseText"));
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
            if (items->currentItem()->text().startsWith(QStringLiteral("qtbase "))) {
                QVERIFY(text->toPlainText().contains(QStringLiteral("LGPL-3.0-only")));
                // Review bundles with Qt source evidence preserve the FTL
                // alternative and acknowledgement rather than choosing GPL.
                if (text->toPlainText().contains(QStringLiteral("For the FreeType alternative")))
                    QVERIFY(text->toPlainText().contains(QStringLiteral("The FreeType Project")));
            }
        }
        QVERIFY(foundChanges);
        const auto identity = dialog.findChild<QLabel *>(QStringLiteral("buildIdentity"))->text();
        QVERIFY(QRegularExpression(QStringLiteral("ビルド日 \\d{4}-\\d{2}-\\d{2} / コミット ")).match(identity).hasMatch());
        QVERIFY(!identity.contains(QStringLiteral("C:\\")));
    }
    void markdownAndPlainTextNavigation()
    {
        smoothmail::AboutDialog dialog;
        auto *items = dialog.findChild<QListWidget *>(QStringLiteral("licenseItems"));
        auto *text = dialog.findChild<QTextBrowser *>(QStringLiteral("licenseText"));
        QVERIFY(items && text);
        QVERIFY(!text->openLinks());
        QVERIFY(!text->openExternalLinks());
        auto add = [items](const QString &path) {
            auto *item = new QListWidgetItem(path, items);
            item->setData(Qt::UserRole, path);
            return item;
        };
        auto *index = add(QStringLiteral("test/index.md"));
        auto *next = add(QStringLiteral("test/sub/next.md"));
        auto *original = add(QStringLiteral("test/original.txt"));
        items->setCurrentItem(index);
        QCOMPARE(text->document()->firstBlock().blockFormat().headingLevel(), 1);
        QVERIFY(!text->toPlainText().contains(QStringLiteral("**Formatted text**")));
        QVERIFY(text->document()->toHtml().contains(QStringLiteral("href=\"sub/next.md\"")));
        text->anchorClicked(QUrl(QStringLiteral("sub/next.md")));
        QCOMPARE(items->currentItem(), next);
        QVERIFY(text->toPlainText().contains(QStringLiteral("Next document")));
        text->anchorClicked(QUrl(QStringLiteral("../index.md#review-fixture")));
        QCOMPARE(items->currentItem(), index);
        text->anchorClicked(QUrl(QStringLiteral("original.txt")));
        QCOMPARE(items->currentItem(), original);
        QVERIFY(text->toPlainText().contains(QStringLiteral("# Literal heading")));
        QVERIFY(text->toPlainText().contains(QStringLiteral("**Not bold** and <b>not HTML</b>.")));
        QCOMPARE(text->document()->firstBlock().blockFormat().headingLevel(), 0);
        text->anchorClicked(QUrl(QStringLiteral("file:///C:/Windows/win.ini")));
        QCOMPARE(items->currentItem(), original);
        text->anchorClicked(QUrl(QStringLiteral("../../missing.md")));
        QCOMPARE(items->currentItem(), original);
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
