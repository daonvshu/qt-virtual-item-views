#include "ui_virtualization.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <virtualitemviews/widgetrecycler.h>

namespace tree_virtualization {

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Every bind replaces the previous node's content, including after scrolling.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(480, 36);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeVirtualizationPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeVirtualizationPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(ui->view);
        for (int group = 0; group < 30; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 150; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        // Overscan is measured in flattened visible rows, not model siblings or depth.
        ui->view->setOverscan(3, 6);
        // This adapter uses the default widget type; limits are per recycler type.
        ui->view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, 32);
        connect(ui->before, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setOverscan(value, ui->after->value());
        });
        connect(ui->after, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setOverscan(ui->before->value(), value);
        });
        connect(ui->pool, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, value);
            ui->view->recycler()->trim();
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] {
            // Clear unbound pooled widgets only; do not delete materialized widgets yourself.
            ui->view->recycler()->clear();
            report();
        });
        connect(ui->view, &viv::VirtualItemView::virtualizationUpdated, this, [this] { report(); });
    }

    void report()
    {
        const auto range = ui->view->visibleItemRange();
        const auto stats = ui->view->stats();
        ui->status->setText(QStringLiteral("映射行=%1 / 屏幕范围=[%2,%3] / 物化=%4 / 池=%5 / 累计创建=%6 / 累计复用=%7")
            .arg(ui->view->visibleRowCount()).arg(range.first).arg(range.last)
            .arg(stats.materializedItems).arg(stats.pooledWidgets).arg(stats.createCount)
            .arg(ui->view->recycler()->reuseCount()));
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_virtualization
