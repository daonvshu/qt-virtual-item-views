#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QAbstractTableModel>
#include <virtualitemviews/widgetrecycler.h>
#include <QTimer>

namespace treetable_virtualization {

class LargeTreeModel : public QAbstractItemModel
{
public:
    explicit LargeTreeModel(QObject *parent) : QAbstractItemModel(parent) {}
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (!hasIndex(row, column, parent))
            return {};
        return createIndex(row, column, parent.isValid() ? quintptr(parent.row() + 1) : quintptr(0));
    }
    QModelIndex parent(const QModelIndex &index) const override
    {
        return index.isValid() && index.internalId() != 0
            ? createIndex(int(index.internalId()) - 1, 0, quintptr(0)) : QModelIndex();
    }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (!parent.isValid())
            return 5;
        return parent.column() == 0 && parent.internalId() == 0 ? 200000 : 0;
    }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return 200; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        return index.internalId() == 0 ? QStringLiteral("分组 %1 / 列 %2").arg(index.row() + 1).arg(index.column() + 1)
            : QStringLiteral("节点 %1.%2 / 列 %3").arg(index.internalId()).arg(index.row() + 1).arg(index.column() + 1);
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) return {};
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section + 1) : QString::number(section + 1);
    }
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
        // Replace old content on every bind; a widget can be reused for a different row or column.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
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
        auto *description = new QLabel(QStringLiteral("20 二维虚拟化与复用：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_rows = new QSpinBox(this);
        m_rows->setRange(0, 12);
        m_rows->setValue(2);
        m_rows->setPrefix(QStringLiteral("行预加载 "));
        controls->addWidget(m_rows);
        m_columns = new QSpinBox(this);
        m_columns->setRange(0, 12);
        m_columns->setValue(1);
        m_columns->setPrefix(QStringLiteral("列预加载 "));
        controls->addWidget(m_columns);
        m_pool = new QSpinBox(this);
        m_pool->setRange(0, 500);
        m_pool->setValue(256);
        m_pool->setPrefix(QStringLiteral("每类型池上限 "));
        controls->addWidget(m_pool);
        m_clear = new QPushButton(QStringLiteral("清空空闲池"), this);
        controls->addWidget(m_clear);
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
        // Disable the row header for this large-model example.
        m_view->setVerticalHeaderVisible(false);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new LargeTreeModel(m_view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        m_view->setColumnWidth(0, 280);
        // Descendants participate only after expansion; overscan counts flattened visible rows.
        m_view->setExpansionAnimationEnabled(false);
        m_view->expand(m_model->index(0, 0));
        m_view->setExpansionAnimationEnabled(true);
        // Configure row and column overscan separately instead of creating widgets for the whole model.
        m_view->setOverscan(2, 2);
        m_view->setColumnOverscan(1);
        m_view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, 256);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            const auto stats = m_view->stats();
            m_status->setText(QStringLiteral("物化格=%1，复用池=%2，可见列=%3，create=%4，bind=%5")
                .arg(m_view->materializedCellIndexes().size()).arg(m_view->recycler()->pooledCount())
                .arg(m_view->visibleColumnLogicalIndexes().size()).arg(stats.createCount).arg(stats.bindCount));
        });
        timer->start(300);

        connect(m_rows, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setOverscan(value, value);
        });
        connect(m_columns, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setColumnOverscan(value);
        });
        connect(m_pool, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, value);
            m_view->recycler()->trim();
        });
        connect(m_clear, &QPushButton::clicked, this, [this] {
            // clear deletes only pooled widgets, leaving materialized and pinned widgets intact.
            m_view->recycler()->clear();
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    LargeTreeModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QSpinBox *m_rows = nullptr;
    QSpinBox *m_columns = nullptr;
    QSpinBox *m_pool = nullptr;
    QPushButton *m_clear = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_virtualization
