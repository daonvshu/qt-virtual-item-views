#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace tree_expansion {

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
        auto *description = new QLabel(QStringLiteral("07 展开：当前节点可展开、收起、切换或递归展开。方向键右/左和 * 也可操作。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        open = new QPushButton(QStringLiteral("展开"), this);
        controls->addWidget(open);
        close = new QPushButton(QStringLiteral("收起"), this);
        controls->addWidget(close);
        toggle = new QPushButton(QStringLiteral("切换"), this);
        controls->addWidget(toggle);
        recursive = new QPushButton(QStringLiteral("递归展开"), this);
        controls->addWidget(recursive);
        all = new QPushButton(QStringLiteral("全部收起"), this);
        controls->addWidget(all);
        controls->addStretch();
        layout->addLayout(controls);
        auto *options = new QHBoxLayout;
        animated = new QCheckBox(QStringLiteral("动画"), this);
        animated->setChecked(true);
        options->addWidget(animated);
        duration = new QSpinBox(this);
        duration->setRange(0, 1000);
        duration->setValue(300);
        duration->setPrefix(QStringLiteral("时长 ms "));
        options->addWidget(duration);
        options->addStretch();
        layout->addLayout(options);
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
            for (int row = 0; row < 8; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 4; ++leaf)
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

        // Expansion animation is opt-in. It changes visuals, not model identities.
        m_view->setExpansionAnimationEnabled(true);
        m_view->setExpansionAnimationDuration(300);
        m_view->setCurrentIndex(m_model->index(0, 0));
        connect(open, &QPushButton::clicked, this, [this] { m_view->expand(m_view->currentIndex()); });
        connect(close, &QPushButton::clicked, this, [this] { m_view->collapse(m_view->currentIndex()); });
        connect(toggle, &QPushButton::clicked, this, [this] { m_view->toggleExpanded(m_view->currentIndex()); });
        connect(recursive, &QPushButton::clicked, this, [this] { m_view->expandRecursively(m_view->currentIndex()); });
        connect(all, &QPushButton::clicked, m_view, &viv::VirtualTreeView::collapseAll);
        connect(animated, &QCheckBox::toggled, m_view, &viv::VirtualTreeView::setExpansionAnimationEnabled);
        connect(duration, QOverload<int>::of(&QSpinBox::valueChanged), m_view,
                &viv::VirtualTreeView::setExpansionAnimationDuration);
        const auto report = [this](const QModelIndex &index) {
            m_status->setText(QStringLiteral("%1 / expanded=%2 / hasChildren=%3 / 可见树节点=%4")
                .arg(index.data().toString()).arg(m_view->isExpanded(index))
                .arg(m_view->hasChildren(index)).arg(m_view->visibleRowCount()));
        };
        connect(m_view, &viv::VirtualTreeView::expanded, this, report);
        connect(m_view, &viv::VirtualTreeView::collapsed, this, report);
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *open = nullptr;
    QPushButton *close = nullptr;
    QPushButton *toggle = nullptr;
    QPushButton *recursive = nullptr;
    QPushButton *all = nullptr;
    QCheckBox *animated = nullptr;
    QSpinBox *duration = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_expansion
