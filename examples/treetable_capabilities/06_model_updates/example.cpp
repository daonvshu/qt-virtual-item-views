#include "ui_model_updates.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>

namespace treetable_model_updates {

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
        // Replace old content on every bind; a widget can be reused for a different row or column.
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableModelUpdatesPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableModelUpdatesPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        ui->view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
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

        connect(ui->edit, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            if (index.isValid()) m_model->setData(index, QStringLiteral("内容已更新"));
        });
        connect(ui->add, &QPushButton::clicked, this, [this] {
            const QModelIndex current = ui->view->currentIndex();
            const QModelIndex parent = current.isValid() ? current.siblingAtColumn(0) : QModelIndex();
            auto *branch = parent.isValid() ? m_model->itemFromIndex(parent) : m_model->invisibleRootItem();
            QList<QStandardItem *> cells;
            for (int column = 0; column < m_model->columnCount(); ++column)
                cells.append(new QStandardItem(QStringLiteral("新增节点 / 列 %1").arg(column + 1)));
            branch->appendRow(cells);
            // Insert signals update the visible mapping; expand the actual parent node.
            if (parent.isValid()) ui->view->expand(parent);
        });
        connect(ui->remove, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            // row() is local to parent(); the same row number can occur at every depth.
            if (index.isValid()) m_model->removeRow(index.row(), index.parent());
        });
        connect(ui->column, &QPushButton::clicked, this, [this] {
            const int column = m_model->columnCount();
            m_model->insertColumn(column);
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("新增列 %1").arg(column + 1));
            // The root defines the schema; update descendant rows to provide the same columns.
            const auto fillColumn = [this, column](const auto &visit, const QModelIndex &parent) -> void {
                auto *branch = parent.isValid() ? m_model->itemFromIndex(parent) : m_model->invisibleRootItem();
                branch->setColumnCount(column + 1);
                for (int row = 0; row < branch->rowCount(); ++row) {
                    branch->setChild(row, column, new QStandardItem(QStringLiteral("新列数据")));
                    visit(visit, m_model->index(row, 0, parent));
                }
            };
            fillColumn(fillColumn, {});
        });
        connect(ui->reset, &QPushButton::clicked, this, [this] {
            m_model->clear();
            m_model->setColumnCount(8);
            m_model->setRowCount(6);
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
                for (int row = 0; row < m_model->rowCount(); ++row)
                    m_model->setData(m_model->index(row, column), QStringLiteral("重置 %1 / %2").arg(row + 1).arg(column + 1));
            }
            appendChildren(m_model);
            ui->view->expand(m_model->index(0, 0));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_model_updates
