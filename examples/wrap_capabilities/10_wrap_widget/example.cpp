#include "example.h"
#include "ui_wrap_widget.h"
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

namespace wrap_wrap_widget {


class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::WrapWrapWidgetPage)
    {
        ui->setupUi(this);
        setupExample();
    }
private:
    void setupExample()
    {
        // This also styles widgets added later or returned with insertWidget().
        ui->view->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: #eef3f8; border: 1px solid #d4dfe9; padding: 0 8px; }"
            "QPushButton:hover { background-color: #e0ebf6; }"
            "QPushButton:pressed { background-color: #cfdeee; }"));
        auto *layout = ui->view->wrapLayout();
        layout->setFixedItemHeight(34);
        layout->setItemSpacing(8);
        layout->setRowSpacing(8);
        layout->setContentsMargins(8, 8, 8, 8);
        // Auto height is enabled by default. In the outer resizable QScrollArea,
        // the bounded content height also supplies this host's layout minimum.
        ui->view->setMaximumHeight(180);
        for (int i = 0; i < 12; ++i) addWidget();
        connect(ui->add, &QPushButton::clicked, this, [this]() { addWidget(); });
        connect(ui->take, &QPushButton::clicked, this, [this]() {
            if (detached) return;
            // takeWidget transfers ownership; unique_ptr covers the not-restored case.
            detached.reset(ui->view->takeWidget(0));
        });
        connect(ui->restore, &QPushButton::clicked, this, [this]() {
            if (detached) ui->view->insertWidget(0, detached.release());
        });
        connect(ui->hide, &QCheckBox::toggled, this, [layout](bool hidden) {
            if (layout->itemAt(0)) layout->itemAt(0)->widget()->setVisible(!hidden);
        });
        connect(ui->clear, &QPushButton::clicked, ui->view, &viv::WrapWidget::clear);
        connect(ui->maximum, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &QWidget::setMaximumHeight);
        ui->status->setText(QStringLiteral("不使用缓存复用；控件事件、编辑状态和 QObject 生命周期都由普通 QWidget 机制管理。"));
    }
    void addWidget()
    {
        ui->view->addWidget(new QPushButton(QStringLiteral("Widget %1 ").arg(++sequence)
            + QString(sequence % 5, QLatin1Char('x'))));
    }

    std::unique_ptr<Ui::WrapWrapWidgetPage> ui;
    std::unique_ptr<QWidget> detached;
    int sequence = 0;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace wrap_wrap_widget
