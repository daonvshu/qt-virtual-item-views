#include "example.h"
#include "ui_wrap_layout.h"
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QPushButton>
#include <QFontMetrics>
#include <QPointer>
#include <memory>
#include <virtualitemviews/wraplayout.h>

namespace wrap_wrap_layout {


class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::WrapWrapLayoutPage)
    {
        ui->setupUi(this);
        setupExample();
    }
private:
    void setupExample()
    {
        // Real widgets supply their own background; scope the style to the wrap host.
        ui->container->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: #eef3f8; border: 1px solid #d4dfe9; padding: 0 8px; }"
            "QPushButton:hover { background-color: #e0ebf6; }"
            "QPushButton:pressed { background-color: #cfdeee; }"));
        // Install the public QLayout directly on an ordinary widget.
        auto *layout = new viv::WrapLayout(ui->container);
        layout->setFixedItemHeight(34);
        layout->setItemSpacing(10);
        layout->setRowSpacing(10);
        layout->setContentsMargins(12, 12, 12, 12);
        for (int i = 0; i < 8; ++i)
            layout->addWidget(new QPushButton(QStringLiteral("Real widget %1").arg(i + 1)));
        connect(ui->add, &QPushButton::clicked, this, [layout]() {
            layout->addWidget(new QPushButton(QStringLiteral("Added widget %1").arg(layout->count() + 1)));
        });
        connect(ui->hide, &QCheckBox::toggled, this, [layout](bool hidden) {
            if (layout->itemAt(0)) layout->itemAt(0)->widget()->setVisible(!hidden);
        });
        ui->status->setText(QStringLiteral("普通布局不创建滚动条；需要最大高度和滚动时使用下一章 WrapWidget。"));
    }

    std::unique_ptr<Ui::WrapWrapLayoutPage> ui;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace wrap_wrap_layout
