#include "ui_virtualization.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableVirtualizationPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableVirtualizationPage> ui;

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
        // Disable the row header for this large-model example.
        ui->view->setVerticalHeaderVisible(false);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new LargeTreeModel(ui->view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        ui->view->setColumnWidth(0, 280);
        // Descendants participate only after expansion; overscan counts flattened visible rows.
        ui->view->setExpansionAnimationEnabled(false);
        ui->view->expand(m_model->index(0, 0));
        ui->view->setExpansionAnimationEnabled(true);
        // Configure row and column overscan separately instead of creating widgets for the whole model.
        ui->view->setOverscan(2, 2);
        ui->view->setColumnOverscan(1);
        ui->view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, 256);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            const auto stats = ui->view->stats();
            ui->status->setText(QStringLiteral("物化格=%1，复用池=%2，可见列=%3，create=%4，bind=%5")
                .arg(ui->view->materializedCellIndexes().size()).arg(ui->view->recycler()->pooledCount())
                .arg(ui->view->visibleColumnLogicalIndexes().size()).arg(stats.createCount).arg(stats.bindCount));
        });
        timer->start(300);

        connect(ui->rows, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setOverscan(value, value);
        });
        connect(ui->columns, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setColumnOverscan(value);
        });
        connect(ui->pool, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->recycler()->setMaxPoolSize(viv::kDefaultWidgetType, value);
            ui->view->recycler()->trim();
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] {
            // clear deletes only pooled widgets, leaving materialized and pinned widgets intact.
            ui->view->recycler()->clear();
        });
    }

    LargeTreeModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_virtualization
