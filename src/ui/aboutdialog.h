#pragma once

#include <QDialog>

namespace smoothmail {
class AboutDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
}
