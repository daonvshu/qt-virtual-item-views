#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("18 复用：只有视口、overscan 和保留项有控件，折叠子树不创建行；可调整复用池。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        before = new QSpinBox(this);
        before->setRange(0, 50);
        before->setValue(3);
        before->setPrefix(QStringLiteral("前预加载 "));
        controls->addWidget(before);
        after = new QSpinBox(this);
        after->setRange(0, 50);
        after->setValue(6);
        after->setPrefix(QStringLiteral("后预加载 "));
        controls->addWidget(after);
        pool = new QSpinBox(this);
        pool->setRange(0, 256);
        pool->setValue(32);
        pool->setPrefix(QStringLiteral("池上限 "));
        controls->addWidget(pool);
        clear = new QPushButton(QStringLiteral("清空池"), this);
        controls->addWidget(clear);
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
        for (int group = 0; group < 30; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 150; ++row) {
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

        // Overscan is measured in flattened visible rows, not model siblings or depth.
        m_view->setOverscan(3, 6);
        // This adapter uses the default widget type; limits are per recycler type.
        m_view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, 32);
        connect(before, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setOverscan(value, after->value());
        });
        connect(after, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setOverscan(before->value(), value);
        });
        connect(pool, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, value);
            m_view->recycler()->trim();
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            // Clear unbound pooled widgets only; do not delete materialized widgets yourself.
            m_view->recycler()->clear();
            report();
        });
        connect(m_view, &viv::VirtualItemView::virtualizationUpdated, this, [this] { report(); });
    }

    void report()
    {
        const auto range = m_view->visibleItemRange();
        const auto stats = m_view->stats();
        m_status->setText(QStringLiteral("映射行=%1 / 屏幕范围=[%2,%3] / 物化=%4 / 池=%5 / 累计创建=%6 / 累计复用=%7")
            .arg(m_view->visibleRowCount()).arg(range.first).arg(range.last)
            .arg(stats.materializedItems).arg(stats.pooledWidgets).arg(stats.createCount)
            .arg(m_view->recycler()->reuseCount()));
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QSpinBox *before = nullptr;
    QSpinBox *after = nullptr;
    QSpinBox *pool = nullptr;
    QPushButton *clear = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_virtualization
