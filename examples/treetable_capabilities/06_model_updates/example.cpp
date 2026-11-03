#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("06 层级模型更新：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_edit = new QPushButton(QStringLiteral("修改当前格"), this);
        controls->addWidget(m_edit);
        m_add = new QPushButton(QStringLiteral("给当前节点加子项"), this);
        controls->addWidget(m_add);
        m_remove = new QPushButton(QStringLiteral("删除当前行"), this);
        controls->addWidget(m_remove);
        m_column = new QPushButton(QStringLiteral("增加列"), this);
        controls->addWidget(m_column);
        m_reset = new QPushButton(QStringLiteral("重置"), this);
        controls->addWidget(m_reset);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeTableView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
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

        connect(m_edit, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            if (index.isValid()) m_model->setData(index, QStringLiteral("内容已更新"));
        });
        connect(m_add, &QPushButton::clicked, this, [this] {
            const QModelIndex current = m_view->currentIndex();
            const QModelIndex parent = current.isValid() ? current.siblingAtColumn(0) : QModelIndex();
            auto *branch = parent.isValid() ? m_model->itemFromIndex(parent) : m_model->invisibleRootItem();
            QList<QStandardItem *> cells;
            for (int column = 0; column < m_model->columnCount(); ++column)
                cells.append(new QStandardItem(QStringLiteral("新增节点 / 列 %1").arg(column + 1)));
            branch->appendRow(cells);
            // Insert signals update the visible mapping; expand the actual parent node.
            if (parent.isValid()) m_view->expand(parent);
        });
        connect(m_remove, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            // row() is local to parent(); the same row number can occur at every depth.
            if (index.isValid()) m_model->removeRow(index.row(), index.parent());
        });
        connect(m_column, &QPushButton::clicked, this, [this] {
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
        connect(m_reset, &QPushButton::clicked, this, [this] {
            m_model->clear();
            m_model->setColumnCount(8);
            m_model->setRowCount(6);
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
                for (int row = 0; row < m_model->rowCount(); ++row)
                    m_model->setData(m_model->index(row, column), QStringLiteral("重置 %1 / %2").arg(row + 1).arg(column + 1));
            }
            appendChildren(m_model);
            m_view->expand(m_model->index(0, 0));
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_column = nullptr;
    QPushButton *m_reset = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_model_updates
