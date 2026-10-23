#include "example.h"
#include <QStringList>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace tree_frozen_rows {

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
        auto *description = new QLabel(QStringLiteral("19 冻结：冻结展开后最前/最后几行；它们是当前可见映射行，不是固定的模型节点身份。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        top = new QSpinBox(this);
        top->setRange(0, 6);
        top->setValue(2);
        top->setPrefix(QStringLiteral("顶部冻结 "));
        controls->addWidget(top);
        bottom = new QSpinBox(this);
        bottom->setRange(0, 6);
        bottom->setValue(1);
        bottom->setPrefix(QStringLiteral("底部冻结 "));
        controls->addWidget(bottom);
        width = new QSpinBox(this);
        width->setRange(0, 6);
        width->setValue(2);
        width->setPrefix(QStringLiteral("区域分隔线 "));
        controls->addWidget(width);
        gaps = new QCheckBox(QStringLiteral("显示行间距"), this);
        gaps->setChecked(false);
        controls->addWidget(gaps);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 8; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 20; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        // Counts refer to flattened visible rows; expand/collapse may change the frozen identities.
        m_view->setFrozenRows(2);
        m_view->setFrozenBottomRows(1);
        m_view->setExpansionAnimationEnabled(true);
        applySeparator();
        connect(top, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualItemView::setFrozenRows);
        connect(bottom, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualItemView::setFrozenBottomRows);
        connect(width, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applySeparator(); });
        connect(gaps, &QCheckBox::toggled, this, [this](bool enabled) { m_view->setRowSpacing(enabled ? 8 : 0); });
        connect(m_view, &viv::VirtualItemView::virtualizationUpdated, this, [this] {
            QStringList ranges;
            // With frozen rows, visible rows form several ranges instead of one contiguous range.
            for (const auto &range : m_view->visibleItemRanges())
                ranges.append(QStringLiteral("[%1,%2]").arg(range.first).arg(range.last));
            m_status->setText(QStringLiteral("实际顶部=%1 / 底部=%2 / 屏幕映射行范围=%3")
                .arg(m_view->frozenRows()).arg(m_view->frozenBottomRows()).arg(ranges.join(QStringLiteral(" + "))));
        });
    }

    void applySeparator()
    {
        viv::PaneSeparatorStyle style;
        style.width = width->value();
        style.color = QColor(QStringLiteral("#2676c8"));
        style.lineStyle = Qt::DashLine;
        // Pane separators differ from per-node grid lines; zero width hides them.
        m_view->setItemPaneSeparatorStyle(style);
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QSpinBox *top = nullptr;
    QSpinBox *bottom = nullptr;
    QSpinBox *width = nullptr;
    QCheckBox *gaps = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_frozen_rows
