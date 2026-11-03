#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QPainter>
#include <QPen>
#include <QSpinBox>

namespace treetable_branch_renderer {

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

class TriangleRenderer : public viv::BranchIndicatorRenderer
{
public:
    explicit TriangleRenderer(int size) : m_size(size) {}
    void paintBranch(QPainter *painter, const viv::BranchIndicatorState &state,
                     const QModelIndex &, const QRect &cell) const override
    {
        // Size is in logical pixels; QPainter applies the paint device's scale factor.
        paintBuiltinBranch(painter, state, cell, QPalette(), m_size,
                           QColor(QStringLiteral("#2676c8")), Qt::AlignCenter, QMargins());
    }
private:
    int m_size;
};

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        // Every bind replaces content from the previous node and logical column.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

// Each chapter owns this helper; no implementation is shared with other pages.
// Only column zero owns children, and every child row supplies the full column schema.
static void appendChildren(QStandardItemModel *model)
{
    const int columns = model->columnCount();
    for (int group = 0; group < model->rowCount(); ++group) {
        auto *branch = model->item(group, 0);
        branch->removeRows(0, branch->rowCount());
        branch->setColumnCount(columns);
        for (int row = 0; row < 8; ++row) {
            QList<QStandardItem *> cells;
            for (int column = 0; column < columns; ++column)
                cells.append(new QStandardItem(QStringLiteral("节点 %1.%2 / 列 %3")
                    .arg(group + 1).arg(row + 1).arg(column + 1)));
            branch->appendRow(cells);
            if (row == 0) {
                for (int leaf = 0; leaf < 3; ++leaf) {
                    QList<QStandardItem *> leaves;
                    for (int column = 0; column < columns; ++column)
                        leaves.append(new QStandardItem(QStringLiteral("叶子 %1.%2.%3 / 列 %4")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1).arg(column + 1)));
                    cells.first()->appendRow(leaves);
                }
            }
        }
    }
}

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
        auto *description = new QLabel(QStringLiteral("11 自定义分支绘制：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        custom = new QCheckBox(QStringLiteral("使用连接线 renderer"), this);
        custom->setChecked(true);
        controls->addWidget(custom);
        size = new QSpinBox(this);
        size->setRange(4, 24);
        size->setValue(10);
        size->setPrefix(QStringLiteral("三角图标大小 "));
        controls->addWidget(size);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeTableView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // A node is identified by its column-zero index, never by its visible row number.
        m_model = new QStandardItemModel(6, 6, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column) {
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
            for (int row = 0; row < m_model->rowCount(); ++row)
                m_model->setData(m_model->index(row, column),
                    QStringLiteral("分组 %1 / 列 %2").arg(row + 1).arg(column + 1));
        }
        appendChildren(m_model);

        // The view owns the adapter through teardown; the QObject parent owns the model.
        // CellWidgets applies tree indentation only to logical column zero.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        m_view->setDefaultColumnWidth(150);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);
        m_view->setColumnWidth(0, 280);

        m_view->setIndentation(24);
        // Ownership transfer keeps the renderer alive until branch painting is finished.
        m_view->setBranchIndicatorRenderer(new ConnectorRenderer, true);
        m_view->expandRecursively(m_model->index(0, 0));
        connect(custom, &QCheckBox::toggled, this, [this](bool enabled) {
            // A custom renderer replaces built-in decoration without affecting expansion or hit testing.
            viv::BranchIndicatorRenderer *renderer = enabled
                ? static_cast<viv::BranchIndicatorRenderer *>(new ConnectorRenderer)
                : static_cast<viv::BranchIndicatorRenderer *>(new TriangleRenderer(size->value()));
            m_view->setBranchIndicatorRenderer(renderer, true);
        });
        connect(size, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            if (!custom->isChecked()) m_view->setBranchIndicatorRenderer(new TriangleRenderer(value), true);
        });
        connect(m_view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            // Omitted cellDepth queries this node's own cell; an explicit depth queries an ancestor cell.
            const auto state = m_view->branchState(index);
            m_status->setText(QStringLiteral("depth=%1 / cellDepth=%2 / children=%3 / siblings=%4 / open=%5")
                .arg(state.itemDepth).arg(state.cellDepth).arg(state.hasChildren)
                .arg(state.hasSiblings).arg(state.isOpen()));
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *custom = nullptr;
    QSpinBox *size = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_branch_renderer
