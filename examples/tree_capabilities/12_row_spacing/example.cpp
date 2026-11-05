#include "ui_row_spacing.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>

namespace tree_row_spacing {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeRowSpacingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeRowSpacingPage> ui;

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
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 12; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 2; ++leaf)
                        node->appendRow(new QStandardItem(QStringLiteral("叶子 %1.%2.%3")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1)));
                }
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Missing/negative below-node data falls back to this default spacing.
        ui->view->setRowSpacing(8);
        ui->view->setSpacingBackgroundColor(QColor(QStringLiteral("#edf3f9")));
        ui->view->expandRecursively(m_model->index(0, 0));
        connect(ui->gap, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setRowSpacing);
        connect(ui->overrides, &QCheckBox::toggled, this, [this](bool enabled) {
            const QModelIndex branch = m_model->index(0, 0);
            for (int row = 0; row < m_model->rowCount(branch); ++row) {
                const QModelIndex index = m_model->index(row, 0, branch);
                // Adjacent nodes add the upper node's below gap to the lower node's above gap.
                m_model->setData(index, enabled ? QVariant(row % 2 ? 4 : 14) : QVariant(),
                    viv::VirtualTreeView::NodeRowSpacingBelowRole);
                m_model->setData(index, enabled ? QVariant(3) : QVariant(),
                    viv::VirtualTreeView::NodeRowSpacingAboveRole);
            }
        });
        connect(ui->extent, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setRowGridLineExtent(static_cast<viv::VirtualTreeView::RowGridLineExtent>(value));
        });
        connect(ui->lines, &QCheckBox::toggled, ui->view, &viv::VirtualTreeView::setRowGridLinesVisible);
        connect(ui->width, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualTreeView::setRowGridLineWidth);
        // Color and width are independent; an invalid color restores the palette's mid color.
        ui->view->setRowGridLineColor(QColor(QStringLiteral("#8aa2bb")));
        ui->extent->setCurrentIndex(2);
        connect(ui->custom, &QCheckBox::toggled, this, [this](bool enabled) {
            if (enabled) {
                // The factory creates; the binder refreshes the gap's upper-node identity on reuse.
                ui->view->setRowSpacingFactory(
                    [](const QModelIndex &, QWidget *parent) -> QWidget * { return new QLabel(parent); },
                    [](QWidget *widget, const QModelIndex &above) {
                        static_cast<QLabel *>(widget)->setText(QStringLiteral("%1 下方间距").arg(above.data().toString()));
                    });
            } else {
                ui->view->setRowSpacingFactory({}, {});
            }
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_row_spacing
