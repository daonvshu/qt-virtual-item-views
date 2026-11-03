#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QItemSelectionModel>
#include <QPersistentModelIndex>

namespace treetable_item_events {

class ActionCell : public QWidget
{
public:
    explicit ActionCell(QWidget *parent) : QWidget(parent) { setupUi(); }
    QPersistentModelIndex boundIndex;
    QLabel *title = nullptr;
    QPushButton *action = nullptr;
private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        title = new QLabel(this);
        action = new QPushButton(QStringLiteral("标记"), this);
        // Keep arrow-key navigation focus on the table when an inline action is clicked.
        action->setFocusPolicy(Qt::NoFocus);
        // Hidden actions leave the full cell width available for the title.
        layout->addWidget(title, 1);
        layout->addWidget(action);
    }
};
class CellAdapter : public viv::CellWidgetAdapter
{
public:
    explicit CellAdapter(viv::VirtualTreeTableView &view) : m_view(view) {}
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *cell = new ActionCell(parent);
        QObject::connect(cell->action, &QPushButton::clicked, cell, [this, cell] {
            // Replace boundIndex on every bind; a persistent index tracks item identity, not widget reuse.
            const QModelIndex index = cell->boundIndex;
            if (index.isValid())
                m_view.model()->setData(index, index.data().toString() + QStringLiteral(" ✓"));
        });
        return cell;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *cell = static_cast<ActionCell *>(widget);
        cell->boundIndex = QPersistentModelIndex(index);
        cell->title->setText(index.data().toString());
    }
    void visualStateChanged(QWidget *widget, const QModelIndex &index) override
    {
        // This chapter always shows actions in column 3; chapter 16 demonstrates state-based visibility.
        static_cast<ActionCell *>(widget)->action->setVisible(index.column() == 2);
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        auto *cell = static_cast<ActionCell *>(widget);
        cell->boundIndex = QPersistentModelIndex();
        cell->title->clear();
        cell->action->hide();
    }
private:
    viv::VirtualTreeTableView &m_view;
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
        auto *description = new QLabel(QStringLiteral("18 行内事件与几何查询：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_query = new QPushButton(QStringLiteral("查询当前格"), this);
        controls->addWidget(m_query);
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
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter(*m_view), true);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(8, 8, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        appendChildren(m_model);
        m_view->setModel(m_model);
        // Expand through the view; model row numbers remain local to each parent.
        m_view->setExpansionAnimationEnabled(false);
        m_view->expandRecursively(m_model->index(0, 0));
        m_view->setExpansionAnimationEnabled(true);
        m_view->setColumnWidth(0, 280);
        m_view->setColumnWidth(2, 240);
        // Item signals such as clicked report model indexes that remain valid when widgets leave the viewport.
        connect(m_view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            m_status->setText(QStringLiteral("clicked row=%1 column=%2 data=%3").arg(index.row()).arg(index.column()).arg(index.data().toString()));
        });
        connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &index, const QModelIndex &) {
                m_status->setText(QStringLiteral("current=%1 / parent=%2 / sibling.row=%3 / column=%4 / visible.row=%5")
                    .arg(index.data().toString()).arg(index.parent().data().toString()).arg(index.row()).arg(index.column())
                    .arg(m_view->visibilityIndex()->visibleRowForIndex(index)));
            });

        connect(m_query, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            if (!index.isValid()) return;
            const QRect rect = m_view->cellRect(index);
            QWidget *widget = m_view->cellWidget(index);
            const QModelIndex bound = widget ? m_view->cellIndexForWidget(widget) : QModelIndex();
            // Call the public base-class indexAt entry; virtual dispatch still uses table cell hit testing.
            const viv::VirtualItemView &itemView = *m_view;
            const QModelIndex hit = itemView.indexAt(rect.center());
            m_status->setText(QStringLiteral("row=%1 column=%2 data=%3；widget=%4；rect=(%5,%6 %7×%8)；hit.row=%9；选中格=%10；widget 绑定=(%11,%12)")
                .arg(index.row()).arg(index.column()).arg(index.data().toString())
                .arg(widget ? QStringLiteral("已物化") : QStringLiteral("未物化"))
                .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()).arg(hit.row())
                .arg(m_view->selectionModel()->selectedIndexes().size()).arg(bound.row()).arg(bound.column()));
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_query = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_item_events
