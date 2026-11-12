#include "accountdialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>

namespace smoothmail {
ServerSettings AccountDialog::ServerFields::value() const
{
    return {host->text().trimmed(), quint16(port->value()), user->text(),
            static_cast<AuthenticationMode>(authentication->currentData().toInt()),
            static_cast<EncryptionMode>(encryption->currentData().toInt())};
}

AccountDialog::ServerFields AccountDialog::createServer(const QString &name, int defaultPort)
{
    auto *box = new QGroupBox(name, this);
    auto *form = new QFormLayout(box);
    ServerFields fields{new QLineEdit(box), new QSpinBox(box), new QLineEdit(box), new QLineEdit(box),
                        new QComboBox(box), new QComboBox(box)};
    fields.host->setObjectName(name + QStringLiteral("Host"));
    fields.port->setObjectName(name + QStringLiteral("Port"));
    fields.user->setObjectName(name + QStringLiteral("User"));
    fields.password->setObjectName(name + QStringLiteral("Password"));
    fields.authentication->setObjectName(name + QStringLiteral("Authentication"));
    fields.encryption->setObjectName(name + QStringLiteral("Encryption"));
    fields.password->setEchoMode(QLineEdit::Password);
    fields.port->setRange(1, 65535);
    fields.port->setValue(defaultPort);
    fields.authentication->addItem(QStringLiteral("PLAIN"), int(AuthenticationMode::Plain));
    fields.authentication->addItem(QStringLiteral("LOGIN"), int(AuthenticationMode::Login));
    fields.authentication->addItem(QStringLiteral("OAuth2"), int(AuthenticationMode::OAuth2));
    fields.encryption->addItem(QStringLiteral("SSL/TLS"), int(EncryptionMode::ImplicitTls));
    fields.encryption->addItem(QStringLiteral("STARTTLS"), int(EncryptionMode::StartTls));
    fields.encryption->addItem(QStringLiteral("なし"), int(EncryptionMode::None));
    form->addRow(QStringLiteral("サーバ"), fields.host);
    form->addRow(QStringLiteral("ポート番号"), fields.port);
    form->addRow(QStringLiteral("アカウント名"), fields.user);
    form->addRow(QStringLiteral("パスワード"), fields.password);
    form->addRow(QStringLiteral("認証方式"), fields.authentication);
    form->addRow(QStringLiteral("暗号化"), fields.encryption);
    connect(fields.authentication, &QComboBox::currentIndexChanged, this, [fields] {
        const auto password = fields.authentication->currentData().toInt() != int(AuthenticationMode::OAuth2);
        fields.password->setEnabled(password);
        if (!password) fields.password->clear();
    });
    qobject_cast<QVBoxLayout *>(layout())->addWidget(box);
    return fields;
}

AccountDialog::AccountDialog(QWidget *parent)
    : QDialog(parent), m_email(new QLineEdit(this)), m_error(new QLabel(this)),
      m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this)),
      m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    setWindowTitle(QStringLiteral("アカウントの追加"));
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_email->setObjectName(QStringLiteral("accountEmail"));
    form->addRow(QStringLiteral("メールアドレス"), m_email);
    layout->addLayout(form);
    m_smtp = createServer(QStringLiteral("SMTP"), 465);
    m_imap = createServer(QStringLiteral("IMAP4"), 993);
    auto *test = new QPushButton(QStringLiteral("接続テスト"), this);
    test->setEnabled(false);
    test->setToolTip(QStringLiteral("サーバ接続層を実装後に利用できます。"));
    layout->addWidget(test);
    m_error->setObjectName(QStringLiteral("accountError"));
    m_error->setTextFormat(Qt::PlainText);
    m_error->setWordWrap(true);
    layout->addWidget(m_error);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("決定"));
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &AccountDialog::reject);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        const auto value = account();
        const auto error = validateAccount(value);
        if (!error.isEmpty()) {
            m_error->setText(error);
            return;
        }
        m_saving = true;
        m_buttons->setEnabled(false);
        m_error->setText(QStringLiteral("秘密情報と設定を保存中..."));
        emit saveRequested(value, m_smtp.password->text(), m_imap.password->text());
    });
}

Account AccountDialog::account() const
{
    return {m_id, m_email->text().trimmed(), m_smtp.value(), m_imap.value()};
}

void AccountDialog::saveFinished(const QString &error)
{
    m_saving = false;
    m_buttons->setEnabled(true);
    if (!error.isEmpty()) {
        m_error->setText(error);
        return;
    }
    m_smtp.password->clear();
    m_imap.password->clear();
    accept();
}

void AccountDialog::reject()
{
    if (m_saving) return;
    m_smtp.password->clear();
    m_imap.password->clear();
    QDialog::reject();
}
} // namespace smoothmail
