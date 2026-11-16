#pragma once
#include "core/systemsettings.h"
#include <QDialog>
class QSpinBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;
namespace smoothmail {
class SystemSettingsDialog final : public QDialog {
    Q_OBJECT
public:
    SystemSettingsDialog(const SystemSettings &settings, const QString &secret, QWidget *parent = nullptr);
    void saved(const QString &error);
signals:
    void saveRequested(const smoothmail::SystemSettings &settings, const QString &secret);
protected:
    void reject() override;
private:
    SystemSettings value() const;
    bool changed() const;
    SystemSettings m_original;
    QString m_secret;
    QSpinBox *m_page, *m_interval, *m_parallel;
    QLineEdit *m_client, *m_password, *m_microsoftClient;
    QCheckBox *m_defaultGoogle, *m_defaultMicrosoft;
    void updateInputs();
    QPushButton *m_save;
    QLabel *m_error;
    bool m_saving = false;
};
}
