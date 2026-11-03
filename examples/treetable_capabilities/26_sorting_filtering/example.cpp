#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSortFilterProxyModel>

namespace treetable_sorting_filtering {

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
        auto *description = new QLabel(QStringLiteral("26 同级排序与递归过滤：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_asc = new QPushButton(QStringLiteral("首列升序"), this);
        controls->addWidget(m_asc);
        m_desc = new QPushButton(QStringLiteral("首列降序"), this);
        controls->addWidget(m_desc);
        m_filter = new QCheckBox(QStringLiteral("仅显示条目名称包含 1 的行"), this);
        m_filter->setChecked(false);
        controls->addWidget(m_filter);
        m_source = new QPushButton(QStringLiteral("查询源索引"), this);
        controls->addWidget(m_source);
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
        m_model = new QStandardItemModel(8, 8, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        m_model->setHeaderData(0, Qt::Horizontal, QStringLiteral("条目名称"));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            const QString rowName = QStringLiteral("条目 %1").arg(row + 1);
            for (int column = 0; column < m_model->columnCount(); ++column) {
                // Keep the filter column free of shared text such as "column 1".
                const QString text = column == 0 ? rowName
                    : QStringLiteral("%1 / 列 %2").arg(rowName).arg(column + 1);
                m_model->setData(m_model->index(row, column), text);
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        appendChildren(m_model);
        m_proxy = new QSortFilterProxyModel(m_view);
        m_proxy->setSourceModel(m_model);
        m_proxy->setDynamicSortFilter(true);
        // Matching descendants keep their ancestor path visible (Qt 5.12 and later).
        m_proxy->setRecursiveFilteringEnabled(true);
        // Filter the displayed row name in the first column only.
        m_proxy->setFilterKeyColumn(0);
        m_proxy->setFilterRole(Qt::DisplayRole);
        // Install the proxy after creating the source model; use mapToSource for indexes received from the view.
        m_view->setModel(m_proxy);
        m_view->setColumnWidth(0, 280);
        m_view->expandRecursively(m_proxy->index(0, 0));
        m_view->setSortingEnabled(true);
        m_view->sortByColumn(0, Qt::AscendingOrder);
        connect(m_view, &viv::VirtualTreeTableView::sortIndicatorRequested, this, [this](int column, Qt::SortOrder order) {
            m_status->setText(QStringLiteral("排序 column=%1 order=%2").arg(column).arg(int(order)));
        });

        connect(m_asc, &QPushButton::clicked, this, [this] {
            m_view->sortByColumn(0, Qt::AscendingOrder);
        });
        connect(m_desc, &QPushButton::clicked, this, [this] {
            m_view->sortByColumn(0, Qt::DescendingOrder);
        });
        connect(m_filter, &QCheckBox::toggled, this, [this](bool enabled) {
            m_proxy->setFilterFixedString(enabled ? QStringLiteral("1") : QString());
            // Expansion uses proxy indexes after every filter change.
            for (int row = 0; row < m_proxy->rowCount(); ++row)
                m_view->expandRecursively(m_proxy->index(row, 0));
            m_status->setText(QStringLiteral("当前显示 %1 / %2 个根节点")
                .arg(m_proxy->rowCount()).arg(m_model->rowCount()));
        });
        connect(m_source, &QPushButton::clicked, this, [this] {
            const QModelIndex source = m_proxy->mapToSource(m_view->currentIndex());
            m_status->setText(QStringLiteral("源节点=%1 / parent=%2 / row=%3 / column=%4").arg(source.data().toString()).arg(source.parent().data().toString()).arg(source.row()).arg(source.column()));
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_asc = nullptr;
    QPushButton *m_desc = nullptr;
    QCheckBox *m_filter = nullptr;
    QPushButton *m_source = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_sorting_filtering
