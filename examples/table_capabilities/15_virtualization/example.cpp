#include "ui_virtualization.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QAbstractTableModel>
#include <virtualitemviews/widgetrecycler.h>
#include <QTimer>

namespace table_virtualization {

class Model : public QAbstractTableModel
{
public:
    explicit Model(QObject *parent) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 100000000; }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 200; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole) return {};
        return QStringLiteral("行 %1 / 列 %2").arg(index.row() + 1).arg(index.column() + 1);
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableVirtualizationPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableVirtualizationPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        ui->view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        // Disable the row header for this large-model example.
        ui->view->setVerticalHeaderVisible(false);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new Model(ui->view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
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

    Model *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_virtualization
