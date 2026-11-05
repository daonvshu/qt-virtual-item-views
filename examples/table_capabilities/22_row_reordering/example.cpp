#include "ui_row_reordering.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <virtualitemviews/reorderabletablemodel.h>

namespace table_row_reordering {

class Model : public viv::ReorderableTableModel
{
public:
    explicit Model(QObject *parent) : viv::ReorderableTableModel(100, 8, parent) {}
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole) return {};
        // sourceRow identifies the data record; index.row() is its current position.
        return QStringLiteral("记录 %1 / 列 %2").arg(sourceRow(index.row()) + 1).arg(index.column() + 1);
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) return {};
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section + 1) : QString::number(sourceRow(section) + 1);
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableRowReorderingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableRowReorderingPage> ui;

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
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new Model(ui->view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // The model must implement moveRows; the default QAbstractItemModel implementation rejects moves.
        ui->view->setVerticalHeaderDragEnabled(true);
        ui->view->setRowFollowsHeaderVisual(true);
        connect(ui->view, &viv::VirtualTableView::rowMoveRequested, this, [this](int from, int to) {
            ui->status->setText(QStringLiteral("行号拖动 from=%1 to=%2").arg(from).arg(to));
        });

        connect(ui->move, &QPushButton::clicked, this, [this] {
            m_model->moveRows({}, 0, 1, {}, 5);
        });
        connect(ui->reset, &QPushButton::clicked, this, [this] {
            m_model->resetRowOrder();
        });
        connect(ui->follow, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setRowFollowsHeaderVisual(enabled);
        });
    }

    Model *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_row_reordering
