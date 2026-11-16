#include "aboutdialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QDesktopServices>
#include <QUrl>
#include <QSplitter>
#include <QVBoxLayout>
#include <QJsonObject>

// The resource is linked from a static library; reference its initializer.
static void initializeLicenseResources()
{
    static bool initialized = false;
    if (!initialized) {
        Q_INIT_RESOURCE(smoothmail_licenses);
        initialized = true;
    }
}

namespace smoothmail {
namespace {
class LicenseBrowser : public QTextBrowser
{
public:
    using QTextBrowser::QTextBrowser;
    QVariant loadResource(int, const QUrl &) override
    {
        // Notices are text-only. Do not let Markdown images read local files
        // or request network resources.
        return {};
    }
};
}

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    initializeLicenseResources();
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(QStringLiteral("このソフトウェアについて"));
    auto *layout = new QVBoxLayout(this);
    auto *name = new QLabel(QStringLiteral("SmoothMail %1").arg(QStringLiteral(SMOOTHMAIL_VERSION)), this);
    name->setTextFormat(Qt::PlainText);
    name->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(name);
    auto *version = new QLabel(QStringLiteral("ビルド日 %1 / コミット %2")
        .arg(QStringLiteral(SMOOTHMAIL_BUILD_DATE), QStringLiteral(SMOOTHMAIL_SOURCE_ID)), this);
    version->setObjectName(QStringLiteral("buildIdentity"));
    version->setTextFormat(Qt::PlainText);
    version->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    version->setWordWrap(true);
    version->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(version);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *items = new QListWidget(splitter);
    items->setObjectName(QStringLiteral("licenseItems"));
    auto *text = new LicenseBrowser(splitter);
    text->setObjectName(QStringLiteral("licenseText"));
    text->setReadOnly(true);
    text->setOpenLinks(false);
    text->setOpenExternalLinks(false);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 620});
    layout->addWidget(splitter, 1);
    QFile catalog(QStringLiteral(":/licenses/catalog.json"));
    if (catalog.open(QIODevice::ReadOnly)) {
        for (const auto &entry : QJsonDocument::fromJson(catalog.readAll()).array()) {
            const auto object = entry.toObject();
            auto *item = new QListWidgetItem(object["title"].toString(), items);
            item->setData(Qt::UserRole, object["file"].toString());
        }
    }
    connect(items, &QListWidget::currentItemChanged, this, [text](QListWidgetItem *item) {
        if (!item) { text->clear(); return; }
        const auto path = item->data(Qt::UserRole).toString();
        const QUrl source(QStringLiteral("qrc:/licenses/") + path);
        QFile file(QStringLiteral(":") + source.path());
        if (!file.open(QIODevice::ReadOnly)) {
            text->setPlainText(QStringLiteral("ライセンス資料を読み込めません。"));
            return;
        }
        const auto content = QString::fromUtf8(file.readAll());
        if (path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
            text->setMarkdown(content);
        else
            text->setPlainText(content);
        text->document()->setBaseUrl(source);
        text->document()->setMetaInformation(QTextDocument::DocumentUrl, source.toString());
    });
    connect(text, &QTextBrowser::anchorClicked, this, [items, text](const QUrl &link) {
        const auto target = text->document()->baseUrl().resolved(link);
        if (target.scheme() == QStringLiteral("https") || target.scheme() == QStringLiteral("http")) {
            QDesktopServices::openUrl(target);
            return;
        }
        auto document = target;
        document.setFragment({});
        // Only catalogued embedded documents can be opened; never read a
        // local file or fetch arbitrary resources via a notice link.
        for (int row = 0; row < items->count(); ++row) {
            const QUrl candidate(QStringLiteral("qrc:/licenses/")
                + items->item(row)->data(Qt::UserRole).toString());
            if (candidate == document) {
                items->setCurrentRow(row);
                if (target.hasFragment()) text->scrollToAnchor(target.fragment());
                return;
            }
        }
    });
    if (items->count()) items->setCurrentRow(0);
    else text->setPlainText(QStringLiteral("ライセンス資料がありません。配布担当者へ確認してください。"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(860, 600);
}
}
