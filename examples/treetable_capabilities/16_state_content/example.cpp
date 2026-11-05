#include "ui_state_content.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>

namespace treetable_state_content {

class ActionCell : public QWidget
{
public:
    explicit ActionCell(QWidget *parent) : QWidget(parent) { setupUi(); }
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
            // Query the current binding when the event occurs; do not capture the first bound index.
            const QModelIndex index = m_view.cellIndexForWidget(cell);
            if (index.isValid())
                m_view.model()->setData(index, index.data().toString() + QStringLiteral(" ✓"));
        });
        return cell;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        auto *cell = static_cast<ActionCell *>(widget);
        // Reserve the button slot only in the action column to keep its title stable on hover.
        // Reset on every bind because pooled cells can be reused in another column.
        auto policy = cell->action->sizePolicy();
        policy.setRetainSizeWhenHidden(index.column() == 2);
        cell->action->setSizePolicy(policy);
        cell->title->setText(index.data().toString());
    }
    void visualStateChanged(QWidget *widget, const QModelIndex &index) override
    {
        // Only update content visibility here; leave state backgrounds and model check values unchanged.
        const auto state = m_view.visualState(index);
        static_cast<ActionCell *>(widget)->action->setVisible(index.column() == 2 && (state.hovered || state.selected));
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        auto *cell = static_cast<ActionCell *>(widget);
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableStateContentPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableStateContentPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // The view owns this adapter so it remains available while widgets are being unbound.
        ui->view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter(*ui->view), true);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(8, 8, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        appendChildren(m_model);
        ui->view->setModel(m_model);
        // Expand through the view; model row numbers remain local to each parent.
        ui->view->setExpansionAnimationEnabled(false);
        ui->view->expandRecursively(m_model->index(0, 0));
        ui->view->setExpansionAnimationEnabled(true);
        ui->view->setColumnWidth(0, 280);
        ui->view->setVisualStateCornerRadius(6);
        ui->view->setColumnWidth(2, 240);
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_state_content
