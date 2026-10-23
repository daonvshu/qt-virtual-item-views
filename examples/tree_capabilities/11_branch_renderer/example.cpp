#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QPainter>
#include <QPen>

namespace tree_branch_renderer {

class ConnectorRenderer : public viv::BranchIndicatorRenderer
{
public:
    void paintBranch(QPainter *painter, const viv::BranchIndicatorState &state,
                     const QModelIndex &, const QRect &cell) const override
    {
        // The view offers both the node's cell and its retained ancestor cells.
        painter->save();
        painter->setPen(QPen(QColor(QStringLiteral("#7c91a8")), 1, Qt::DotLine));
        const QPoint center = cell.center();
        if (!state.adjoinsItem) {
            if (state.hasSiblings)
                painter->drawLine(center.x(), cell.top(), center.x(), cell.bottom());
        } else {
            painter->drawLine(center.x(), cell.top(), center.x(), center.y());
            if (state.hasSiblings)
                painter->drawLine(center.x(), center.y(), center.x(), cell.bottom());
            painter->drawLine(center.x(), center.y(), cell.right(), center.y());
            if (state.hasChildren) {
                const QRect box(center.x() - 5, center.y() - 5, 10, 10);
                painter->fillRect(box, QColor(QStringLiteral("#eff5ff")));
                painter->setPen(QColor(QStringLiteral("#2676c8")));
                painter->drawRect(box);
                painter->drawLine(center.x() - 3, center.y(), center.x() + 3, center.y());
                if (!state.isExpanded)
                    painter->drawLine(center.x(), center.y() - 3, center.x(), center.y() + 3);
            }
        }
        painter->restore();
    }
};

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
        auto *description = new QLabel(QStringLiteral("11 自定义分支：绘制祖先连接线和加减框，普通行控件无需绘制树装饰。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        custom = new QCheckBox(QStringLiteral("使用连接线 renderer"), this);
        custom->setChecked(true);
        controls->addWidget(custom);
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
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 7; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 3; ++leaf)
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

        m_view->setIndentation(24);
        // Ownership transfer keeps the renderer alive until branch painting is finished.
        m_view->setBranchIndicatorRenderer(new ConnectorRenderer, true);
        m_view->expandRecursively(m_model->index(0, 0));
        connect(custom, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setBranchIndicatorRenderer(enabled ? new ConnectorRenderer : nullptr, true);
        });
        connect(m_view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            // Omitted cellDepth queries this node's own cell; an explicit depth queries an ancestor cell.
            const auto state = m_view->branchState(index);
            m_status->setText(QStringLiteral("depth=%1 / cellDepth=%2 / children=%3 / siblings=%4 / open=%5")
                .arg(state.itemDepth).arg(state.cellDepth).arg(state.hasChildren)
                .arg(state.hasSiblings).arg(state.isOpen()));
        });
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *custom = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_branch_renderer
