#include "ui_root_index.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QComboBox>
#include <QLabel>
#include <QStandardItemModel>

namespace treetable_root_index {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableRootIndexPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableRootIndexPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // A node is identified by its column-zero index, never by its visible row number.
        m_model = new QStandardItemModel(6, 6, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column) {
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
            for (int row = 0; row < m_model->rowCount(); ++row)
                m_model->setData(m_model->index(row, column),
                    QStringLiteral("分组 %1 / 列 %2").arg(row + 1).arg(column + 1));
        }
        appendChildren(m_model);

        // The view owns the adapter through teardown; the QObject parent owns the model.
        // CellWidgets applies tree indentation only to logical column zero.
        ui->view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        ui->view->setDefaultColumnWidth(150);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);
        ui->view->setColumnWidth(0, 280);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        connect(ui->root, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            // Invalid index displays the whole model; a valid root must belong to this model.
            ui->view->setRootIndex(value == 0 ? QModelIndex() : m_model->index(value - 1, 0));
            ui->view->setCurrentIndex(m_model->index(0, 0, ui->view->rootIndex()));
        });
        connect(ui->view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            ui->status->setText(QStringLiteral("当前显示根=%1 / 节点=%2 / 相对深度=%3 / 可见映射行=%4")
                .arg(ui->view->rootIndex().data().toString()).arg(index.data().toString())
                .arg(ui->view->itemDepth(index)).arg(ui->view->visibilityIndex()->visibleRowForIndex(index)));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_root_index
