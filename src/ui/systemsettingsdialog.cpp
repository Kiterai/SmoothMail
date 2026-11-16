#include "systemsettingsdialog.h"
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QCheckBox>
#include <QGroupBox>
namespace smoothmail {
SystemSettingsDialog::SystemSettingsDialog(const SystemSettings &settings, const QString &secret, QWidget *parent)
    : QDialog(parent), m_original(settings), m_secret(secret)
{
    setWindowTitle(QStringLiteral("システム設定"));
    auto *layout = new QFormLayout(this);
    m_page = new QSpinBox(this); m_page->setObjectName(QStringLiteral("settingsPageSize")); m_page->setRange(1, 1000); m_page->setValue(settings.pageSize);
    m_interval = new QSpinBox(this); m_interval->setObjectName(QStringLiteral("settingsInterval")); m_interval->setRange(1, 86400); m_interval->setValue(settings.refreshSeconds);
    m_parallel = new QSpinBox(this); m_parallel->setObjectName(QStringLiteral("settingsParallelUpdates")); m_parallel->setRange(1, 32); m_parallel->setValue(settings.parallelUpdates);
    m_client = new QLineEdit(settings.googleClientId, this); m_client->setObjectName(QStringLiteral("settingsGoogleClient"));
    m_password = new QLineEdit(secret, this); m_password->setObjectName(QStringLiteral("settingsGoogleSecret")); m_password->setEchoMode(QLineEdit::Password);
    layout->addRow(QStringLiteral("表示設定：1ページに表示する件数"), m_page);
    layout->addRow(QStringLiteral("更新処理設定：更新処理の並列数"), m_parallel);
    layout->addRow(QStringLiteral("更新処理設定：自動更新周期（秒）"), m_interval);
    auto *oauth = new QGroupBox(QStringLiteral("OAuth設定"), this);
    auto *oauthLayout = new QFormLayout(oauth);
    auto *google = new QGroupBox(QStringLiteral("Gmail"), oauth);
    auto *googleLayout = new QFormLayout(google);
    m_defaultGoogle = new QCheckBox(QStringLiteral("デフォルトのOAuthクライアントを使用"), google);
    m_defaultGoogle->setObjectName(QStringLiteral("settingsDefaultGoogle"));
    m_defaultGoogle->setChecked(settings.useDefaultGoogleClient);
    googleLayout->addRow(m_defaultGoogle);
    googleLayout->addRow(QStringLiteral("OAuth クライアントID"), m_client);
    googleLayout->addRow(QStringLiteral("OAuth シークレット"), m_password);
    oauthLayout->addRow(google);
    auto *microsoft = new QGroupBox(QStringLiteral("Outlook"), oauth);
    auto *microsoftLayout = new QFormLayout(microsoft);
    m_defaultMicrosoft = new QCheckBox(QStringLiteral("デフォルトのOAuthクライアントを使用"), microsoft);
    m_defaultMicrosoft->setObjectName(QStringLiteral("settingsDefaultMicrosoft"));
    m_defaultMicrosoft->setChecked(settings.useDefaultMicrosoftClient);
    m_microsoftClient = new QLineEdit(settings.microsoftClientId, microsoft);
    m_microsoftClient->setObjectName(QStringLiteral("settingsMicrosoftClient"));
    microsoftLayout->addRow(m_defaultMicrosoft);
    microsoftLayout->addRow(QStringLiteral("OAuth クライアントID"), m_microsoftClient);
    oauthLayout->addRow(microsoft); layout->addRow(oauth);
    m_error = new QLabel(this); m_error->setTextFormat(Qt::PlainText); m_error->setWordWrap(true); layout->addRow(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this); layout->addRow(buttons); m_save = buttons->button(QDialogButtonBox::Save); m_save->setEnabled(false);
    const auto update = [this] { m_save->setEnabled(!m_saving && changed()); };
    connect(m_page, &QSpinBox::valueChanged, this, update); connect(m_interval, &QSpinBox::valueChanged, this, update);
    connect(m_parallel, &QSpinBox::valueChanged, this, update);
    connect(m_client, &QLineEdit::textChanged, this, update); connect(m_password, &QLineEdit::textChanged, this, update);
    connect(m_microsoftClient, &QLineEdit::textChanged, this, update);
    for (auto *checkbox : {m_defaultGoogle, m_defaultMicrosoft})
        connect(checkbox, &QCheckBox::toggled, this, [this, update] { updateInputs(); update(); });
    updateInputs();
    connect(buttons, &QDialogButtonBox::rejected, this, &SystemSettingsDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, buttons] { if (!changed()) return; m_saving = true; buttons->setEnabled(false); m_page->setEnabled(false); m_interval->setEnabled(false); m_parallel->setEnabled(false); updateInputs(); emit saveRequested(value(), m_password->text()); });
    resize(560, sizeHint().height());
}
SystemSettings SystemSettingsDialog::value() const { return {m_page->value(), m_interval->value(), m_client->text().trimmed(), m_parallel->value(), m_microsoftClient->text().trimmed(), m_defaultGoogle->isChecked(), m_defaultMicrosoft->isChecked()}; }
void SystemSettingsDialog::updateInputs()
{
    m_defaultGoogle->setEnabled(!m_saving); m_defaultMicrosoft->setEnabled(!m_saving);
    m_client->setEnabled(!m_saving && !m_defaultGoogle->isChecked());
    m_password->setEnabled(!m_saving && !m_defaultGoogle->isChecked());
    m_microsoftClient->setEnabled(!m_saving && !m_defaultMicrosoft->isChecked());
}
bool SystemSettingsDialog::changed() const { return value() != m_original || m_password->text() != m_secret; }
void SystemSettingsDialog::saved(const QString &error) { m_saving = false; if (error.isEmpty()) { accept(); return; } m_error->setText(error); findChild<QDialogButtonBox *>()->setEnabled(true); m_page->setEnabled(true); m_interval->setEnabled(true); m_parallel->setEnabled(true); updateInputs(); m_save->setEnabled(changed()); }
void SystemSettingsDialog::reject() { if (m_saving) return; if (changed() && QMessageBox::question(this, QStringLiteral("変更を破棄"), QStringLiteral("保存していない変更を破棄しますか？"), QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) return; QDialog::reject(); }
}
