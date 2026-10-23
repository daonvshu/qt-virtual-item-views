#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("12 间距：节点下方间距可覆盖默认值，再加下一节点上方间距；分割线范围可单独配置。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        gap = new QSpinBox(this);
        gap->setRange(0, 36);
        gap->setValue(8);
        gap->setPrefix(QStringLiteral("默认间距 "));
        controls->addWidget(gap);
        overrides = new QCheckBox(QStringLiteral("节点上下间距"), this);
        overrides->setChecked(false);
        controls->addWidget(overrides);
        extent = new QComboBox(this);
        extent->addItems({QStringLiteral("仅节点"), QStringLiteral("节点及图标"), QStringLiteral("整行")});
        controls->addWidget(extent);
        controls->addStretch();
        layout->addLayout(controls);
        auto *options = new QHBoxLayout;
        lines = new QCheckBox(QStringLiteral("分割线"), this);
        lines->setChecked(true);
        options->addWidget(lines);
        width = new QSpinBox(this);
        width->setRange(1, 8);
        width->setValue(1);
        width->setPrefix(QStringLiteral("线宽 "));
        options->addWidget(width);
        custom = new QCheckBox(QStringLiteral("自定义间距控件"), this);
        custom->setChecked(false);
        options->addWidget(custom);
        options->addStretch();
        layout->addLayout(options);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
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
        m_view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Missing/negative below-node data falls back to this default spacing.
        m_view->setRowSpacing(8);
        m_view->setSpacingBackgroundColor(QColor(QStringLiteral("#edf3f9")));
        m_view->expandRecursively(m_model->index(0, 0));
        connect(gap, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualItemView::setRowSpacing);
        connect(overrides, &QCheckBox::toggled, this, [this](bool enabled) {
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
        connect(extent, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->setRowGridLineExtent(static_cast<viv::VirtualTreeView::RowGridLineExtent>(value));
        });
        connect(lines, &QCheckBox::toggled, m_view, &viv::VirtualTreeView::setRowGridLinesVisible);
        connect(width, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualTreeView::setRowGridLineWidth);
        // Color and width are independent; an invalid color restores the palette's mid color.
        m_view->setRowGridLineColor(QColor(QStringLiteral("#8aa2bb")));
        extent->setCurrentIndex(2);
        connect(custom, &QCheckBox::toggled, this, [this](bool enabled) {
            if (enabled) {
                // The factory creates; the binder refreshes the gap's upper-node identity on reuse.
                m_view->setRowSpacingFactory(
                    [](const QModelIndex &, QWidget *parent) -> QWidget * { return new QLabel(parent); },
                    [](QWidget *widget, const QModelIndex &above) {
                        static_cast<QLabel *>(widget)->setText(QStringLiteral("%1 下方间距").arg(above.data().toString()));
                    });
            } else {
                m_view->setRowSpacingFactory({}, {});
            }
        });
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QSpinBox *gap = nullptr;
    QCheckBox *overrides = nullptr;
    QComboBox *extent = nullptr;
    QCheckBox *lines = nullptr;
    QSpinBox *width = nullptr;
    QCheckBox *custom = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_row_spacing
