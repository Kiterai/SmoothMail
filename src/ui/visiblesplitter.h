#pragma once

#include <QPainter>
#include <QSplitter>
#include <QSplitterHandle>

namespace smoothmail {
class VisibleSplitter final : public QSplitter
{
public:
    explicit VisibleSplitter(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSplitter(orientation, parent)
    {
        setHandleWidth(7);
    }
protected:
    QSplitterHandle *createHandle() override
    {
        class Handle final : public QSplitterHandle {
        public:
            Handle(Qt::Orientation orientation, QSplitter *parent) : QSplitterHandle(orientation, parent)
            {
                setAccessibleName(QStringLiteral("ペインの境界（ドラッグでサイズ変更）"));
            }
        protected:
            void paintEvent(QPaintEvent *) override
            {
                QPainter painter(this);
                const auto background = palette().color(QPalette::Window);
                painter.fillRect(rect(), background);
                painter.setPen(QPen(qGray(background.rgb()) < 128 ? Qt::white : Qt::black, 2));
                if (orientation() == Qt::Horizontal)
                    painter.drawLine(width() / 2, 2, width() / 2, height() - 3);
                else
                    painter.drawLine(2, height() / 2, width() - 3, height() / 2);
            }
        };
        return new Handle(orientation(), this);
    }
};
}
