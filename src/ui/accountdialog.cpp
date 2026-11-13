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
#include <QScrollArea>
#include <QScreen>
#include <QStylePainter>
#include <QStyleOptionButton>

namespace smoothmail {
namespace {
class ConnectionTestButton final : public QPushButton
{
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this); QStyleOptionButton option; initStyleOption(&option); option.text.clear();
        painter.drawControl(QStyle::CE_PushButton, option);
        const QString label = QStringLiteral("接続テスト: "), status = property("testPassed").toBool() ? QStringLiteral("✓") : QStringLiteral("未");
        auto content = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
        const int width = fontMetrics().horizontalAdvance(label), total = width + fontMetrics().horizontalAdvance(status);
        const int x = content.center().x() - total / 2;
        painter.setPen(option.palette.color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText));
        painter.drawText(QRect(x, content.top(), width, content.height()), Qt::AlignVCenter | Qt::AlignLeft, label);
        painter.setPen(property("testPassed").toBool() ? QColor(QStringLiteral("#008000")) : QColor(QStringLiteral("#c00000")));
        painter.drawText(QRect(x + width, content.top(), total - width + 2, content.height()), Qt::AlignVCenter | Qt::AlignLeft, status);
    }
};
}
ServerSettings AccountDialog::ServerFields::value() const
{
    return {host->text().trimmed(), quint16(port->value()), user->text(),
            static_cast<AuthenticationMode>(authentication->currentData().toInt()),
            static_cast<EncryptionMode>(encryption->currentData().toInt()),
            provider->currentData().toString(), clientId};
}

QString AccountDialog::ServerFields::secret() const
{
    return authentication->currentData().toInt() == int(AuthenticationMode::OAuth2) ? oauthGrant : password->text();
}

AccountDialog::ServerFields AccountDialog::createServer(const QString &name, int defaultPort)
{
    auto *box = new QGroupBox(name, this);
    auto *form = new QFormLayout(box);
    ServerFields fields{new QLineEdit(box), new QSpinBox(box), new QLineEdit(box), new QLineEdit(box),
                        new QComboBox(box), new QComboBox(box), new QComboBox(box), {}, {}};
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
    fields.provider->addItem(QStringLiteral("Google"), QStringLiteral("google"));
    fields.provider->addItem(QStringLiteral("Microsoft"), QStringLiteral("microsoft"));
    form->addRow(QStringLiteral("OAuthプロバイダ"), fields.provider);
    fields.provider->setEnabled(false);
    connect(fields.authentication, &QComboBox::currentIndexChanged, this, [fields] {
        const auto password = fields.authentication->currentData().toInt() != int(AuthenticationMode::OAuth2);
        fields.password->setEnabled(password);
        fields.provider->setEnabled(!password);
        if (!password) fields.password->clear();
    });
    const auto clearGrant = [this, name] { auto &server = name == QStringLiteral("SMTP") ? m_smtp : m_imap; server.oauthGrant.clear(); server.clientId.clear(); invalidateTest(); };
    connect(fields.authentication, &QComboBox::currentIndexChanged, this, clearGrant);
    connect(fields.provider, &QComboBox::currentIndexChanged, this, clearGrant);
    connect(fields.user, &QLineEdit::textChanged, this, clearGrant);
    for (auto *input : {fields.host, fields.user, fields.password}) connect(input, &QLineEdit::textChanged, this, &AccountDialog::invalidateTest);
    connect(fields.port, &QSpinBox::valueChanged, this, &AccountDialog::invalidateTest);
    connect(fields.encryption, &QComboBox::currentIndexChanged, this, &AccountDialog::invalidateTest);
    m_serverLayout->addWidget(box);
    return fields;
}

AccountDialog::AccountDialog(QWidget *parent)
    : QDialog(parent), m_email(new QLineEdit(this)), m_error(new QLabel(this)),
      m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this)),
      m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    setWindowTitle(QStringLiteral("アカウントの追加"));
    setModal(true);
    const auto available = screen()->availableGeometry().size();
    resize(qMin(560, available.width() - 80), qMin(700, available.height() - 80));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_email->setObjectName(QStringLiteral("accountEmail"));
    form->addRow(QStringLiteral("メールアドレス"), m_email);
    layout->addLayout(form);
    auto *scroll = new QScrollArea(this);
    auto *servers = new QWidget(scroll);
    m_serverLayout = new QVBoxLayout(servers);
    scroll->setWidget(servers); scroll->setWidgetResizable(true); layout->addWidget(scroll, 1);
    m_imap = createServer(QStringLiteral("IMAP4"), 993);
    m_smtp = createServer(QStringLiteral("SMTP"), 465);
    m_test = new ConnectionTestButton(QStringLiteral("接続テスト: 未"), this);
    auto testFont = m_test->font(); testFont.setBold(true); m_test->setFont(testFont);
    m_test->setObjectName(QStringLiteral("connectionTest"));
    layout->addWidget(m_test);
    m_error->setObjectName(QStringLiteral("accountError"));
    m_error->setTextFormat(Qt::PlainText);
    m_error->setWordWrap(true);
    layout->addWidget(m_error);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("決定"));
    layout->addWidget(m_buttons);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_email, &QLineEdit::textChanged, this, &AccountDialog::invalidateTest);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &AccountDialog::reject);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!m_testPassed || m_saving) return;
        const auto value = account();
        const auto error = validateAccount(value);
        if (!error.isEmpty()) {
            m_error->setText(error);
            return;
        }
        setBusy(true);
        m_error->setText(QStringLiteral("秘密情報と設定を保存中..."));
        emit saveRequested(value, m_smtp.secret(), m_imap.secret());
    });
    connect(m_test, &QPushButton::clicked, this, [this] {
        const auto value = account();
        const auto error = validateAccount(value, false);
        if (!error.isEmpty()) { m_error->setText(error); return; }
        invalidateTest(); m_testing = true; setBusy(true); continueTest();
    });
}

Account AccountDialog::account() const
{
    return {m_id, m_email->text().trimmed(), m_smtp.value(), m_imap.value()};
}

void AccountDialog::setAccount(const Account &value)
{
    m_id = value.id; m_email->setText(value.email);
    setWindowTitle(QStringLiteral("アカウントの編集"));
    const auto populate = [](ServerFields &fields, const ServerSettings &settings) {
        fields.host->setText(settings.host); fields.port->setValue(settings.port); fields.user->setText(settings.user);
        fields.authentication->setCurrentIndex(fields.authentication->findData(int(settings.authentication)));
        fields.encryption->setCurrentIndex(fields.encryption->findData(int(settings.encryption)));
        fields.provider->setCurrentIndex(fields.provider->findData(settings.oauthProvider));
        fields.clientId = settings.oauthClientId;
    };
    populate(m_imap, value.imap); populate(m_smtp, value.smtp);
}
void AccountDialog::setSecret(bool smtp, const QString &secret)
{
    auto &fields = smtp ? m_smtp : m_imap;
    if (fields.value().authentication == AuthenticationMode::OAuth2) setOAuthGrant(smtp, secret);
    else fields.password->setText(secret);
}

void AccountDialog::saveFinished(const QString &error)
{
    setBusy(false);
    if (!error.isEmpty()) {
        m_error->setText(error);
        return;
    }
    m_smtp.password->clear();
    m_imap.password->clear();
    accept();
}

void AccountDialog::testFinished(const QString &smtpError, const QString &imapError)
{
    if (m_authorizing) return;
    if (!m_testing) { if (!smtpError.isEmpty() || !imapError.isEmpty()) { invalidateTest(); m_error->setText(smtpError + u'\n' + imapError); } return; }
    m_testing = false; m_testPassed = smtpError.isEmpty() && imapError.isEmpty();
    if (!smtpError.isEmpty() && m_smtp.value().authentication == AuthenticationMode::OAuth2) m_smtp.oauthGrant.clear();
    if (!imapError.isEmpty() && m_imap.value().authentication == AuthenticationMode::OAuth2) m_imap.oauthGrant.clear();
    m_test->setProperty("testPassed", m_testPassed); m_test->setText(m_testPassed ? QStringLiteral("接続テスト: ✓") : QStringLiteral("接続テスト: 未"));
    setBusy(false);
    QStringList errors;
    if (!smtpError.isEmpty()) errors.append(QStringLiteral("SMTP接続に失敗しました。") + smtpError);
    if (!imapError.isEmpty()) errors.append(QStringLiteral("IMAP4接続に失敗しました。") + imapError);
    m_error->setText(errors.isEmpty() ? QStringLiteral("接続に成功しました。") : errors.join(u'\n'));
}

void AccountDialog::reject()
{
    if (m_authorizing) { m_authorizing = false; m_testing = false; emit oauthCancelled(); setBusy(false); }
    if (m_saving) return;
    m_smtp.password->clear();
    m_imap.password->clear();
    QDialog::reject();
}

void AccountDialog::setBusy(bool busy)
{
    m_saving = busy;
    m_email->setEnabled(!busy);
    m_buttons->setEnabled(!busy || m_authorizing);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!busy && m_testPassed);
    m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(!busy || m_authorizing);
    m_test->setEnabled(!busy);
    for (const auto *fields : {&m_smtp, &m_imap}) {
        fields->host->setEnabled(!busy);
        fields->port->setEnabled(!busy);
        fields->user->setEnabled(!busy);
        fields->authentication->setEnabled(!busy);
        fields->encryption->setEnabled(!busy);
        fields->password->setEnabled(!busy && fields->authentication->currentData().toInt() != int(AuthenticationMode::OAuth2));
        const bool oauth = fields->authentication->currentData().toInt() == int(AuthenticationMode::OAuth2);
        fields->provider->setEnabled(!busy && oauth);
    }
    if (m_authorizing) m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(true);
}

void AccountDialog::setOAuthGrant(bool smtp, const QString &grant, const QString &clientId)
{
    invalidateTest();
    auto &fields = smtp ? m_smtp : m_imap;
    fields.oauthGrant = grant;
    if (!clientId.isEmpty()) fields.clientId = clientId;
}
void AccountDialog::oauthFinished(bool smtp, const QString &grant, const QString &clientId, const QString &error)
{
    if (!m_testing || !m_authorizing || smtp != m_authorizingSmtp) return;
    m_authorizing = false;
    if (!error.isEmpty() || grant.isEmpty() || clientId.isEmpty()) { m_testing = false; setBusy(false); m_error->setText(error.isEmpty() ? QStringLiteral("OAuth認可の情報が不足しています。") : error); return; }
    setOAuthGrant(smtp, grant, clientId); continueTest();
}
void AccountDialog::invalidateTest()
{
    m_testPassed = false;
    if (!m_test) return;
    m_test->setProperty("testPassed", false); m_test->setText(QStringLiteral("接続テスト: 未"));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
}
void AccountDialog::continueTest()
{
    for (const bool smtp : {true, false}) {
        const auto &fields = smtp ? m_smtp : m_imap;
        if (fields.value().authentication != AuthenticationMode::OAuth2 || (!fields.secret().isEmpty() && !fields.clientId.isEmpty())) continue;
        m_authorizing = true; m_authorizingSmtp = smtp; setBusy(true);
        m_error->setText(QStringLiteral("外部ブラウザでOAuth認可を完了してください。"));
        emit oauthRequested(smtp, fields.provider->currentData().toString(), fields.user->text()); return;
    }
    const auto value = account(); const auto error = validateAccount(value);
    if (!error.isEmpty()) { m_testing = false; setBusy(false); m_error->setText(error); return; }
    setBusy(true); m_error->setText(QStringLiteral("指定された設定で接続テスト中..."));
    emit testRequested(value, m_smtp.secret(), m_imap.secret());
}
} // namespace smoothmail
