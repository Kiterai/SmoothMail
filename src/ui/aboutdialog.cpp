#include "aboutdialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
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
AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    initializeLicenseResources();
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(QStringLiteral("このソフトウェアについて"));
    auto *layout = new QVBoxLayout(this);
    auto *name = new QLabel(QStringLiteral("SmoothMail %1").arg(QStringLiteral(SMOOTHMAIL_VERSION)), this);
    name->setTextFormat(Qt::PlainText);
    layout->addWidget(name);
    auto *version = new QLabel(QStringLiteral("ビルド日 %1 / コミット %2")
        .arg(QStringLiteral(SMOOTHMAIL_BUILD_DATE), QStringLiteral(SMOOTHMAIL_SOURCE_ID)), this);
    version->setObjectName(QStringLiteral("buildIdentity"));
    version->setTextFormat(Qt::PlainText);
    version->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    version->setWordWrap(true);
    layout->addWidget(version);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *items = new QListWidget(splitter);
    items->setObjectName(QStringLiteral("licenseItems"));
    auto *text = new QPlainTextEdit(splitter);
    text->setObjectName(QStringLiteral("licenseText"));
    text->setReadOnly(true);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 620});
    layout->addWidget(splitter);
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
        QFile file(QStringLiteral(":/licenses/") + item->data(Qt::UserRole).toString());
        text->setPlainText(file.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(file.readAll()) : QStringLiteral("ライセンス資料を読み込めません。"));
    });
    if (items->count()) items->setCurrentRow(0);
    else text->setPlainText(QStringLiteral("ライセンス資料がありません。配布担当者へ確認してください。"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(860, 600);
}
}
