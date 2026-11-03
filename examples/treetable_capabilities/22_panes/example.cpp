#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace treetable_panes {

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
        auto *description = new QLabel(QStringLiteral("22 独立横向滚动分组：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_group0 = new QSpinBox(this);
        m_group0->setRange(0, 1000);
        m_group0->setValue(0);
        m_group0->setPrefix(QStringLiteral("组 0 偏移 "));
        controls->addWidget(m_group0);
        m_group1 = new QSpinBox(this);
        m_group1->setRange(0, 1000);
        m_group1->setValue(0);
        m_group1->setPrefix(QStringLiteral("组 1 偏移 "));
        controls->addWidget(m_group1);
        m_query = new QPushButton(QStringLiteral("查询 pane"), this);
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
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(8, 12, m_view);
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
        viv::TablePaneSpec left;
        left.logicalColumns = {0};
        left.scroll = viv::PaneScroll::Frozen;
        viv::TablePaneSpec first;
        first.logicalColumns = {1, 2, 3, 4, 5};
        first.scrollGroup = 0;
        viv::TablePaneSpec second;
        second.logicalColumns = {6, 7, 8, 9, 10, 11};
        second.scrollGroup = 1;
        // The main scrollbar drives the first scrollable pane; use the group overload for other groups.
        // Horizontal wheel input and Shift+wheel automatically target the pane under the pointer.
        m_view->setPanes({left, first, second});

        connect(m_group0, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setHorizontalOffset(0, value);
        });
        connect(m_group1, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setHorizontalOffset(1, value);
        });
        connect(m_query, &QPushButton::clicked, this, [this] {
            m_status->setText(QStringLiteral("pane=%1，滚动组=%2，主组=%3").arg(m_view->paneSpecs().size()).arg(m_view->scrollGroups().size()).arg(m_view->primaryScrollGroup()));
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QSpinBox *m_group0 = nullptr;
    QSpinBox *m_group1 = nullptr;
    QPushButton *m_query = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_panes
