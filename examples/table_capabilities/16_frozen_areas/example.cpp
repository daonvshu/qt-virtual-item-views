#include "ui_frozen_areas.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/reorderabletablemodel.h>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>

namespace table_frozen_areas {

class Model : public viv::ReorderableTableModel
{
public:
    explicit Model(QObject *parent) : viv::ReorderableTableModel(120, 8, parent) {}
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        // Read the stable record identity so content follows committed row moves.
        return QStringLiteral("条目 %1 / 列 %2").arg(sourceRow(index.row()) + 1).arg(index.column() + 1);
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return {};
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section + 1)
                                             : QString::number(sourceRow(section) + 1);
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableFrozenAreasPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableFrozenAreasPage> ui;

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
        // Header dragging reorders columns; row dragging needs the model's moveRows support.
        ui->view->setColumnDragEnabled(true);
        ui->view->setVerticalHeaderDragEnabled(true);
        // Section edges resize column widths and row heights, including frozen header panes.
        ui->view->setColumnResizeEnabled(true);
        ui->view->setVerticalHeaderResizeEnabled(true);
        // Frozen column sets use logical indexes and are unaffected by visual reordering.
        ui->view->setFrozenColumns({0, 1});
        ui->view->setFrozenRightColumns({6,7});
        ui->view->setFrozenRows(2);
        ui->view->setFrozenBottomRows(1);
        applySeparator(2);

        connect(ui->top, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setFrozenRows(value);
        });
        connect(ui->bottom, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setFrozenBottomRows(value);
        });
        connect(ui->width, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            applySeparator(value);
        });
        connect(ui->columns, &QCheckBox::toggled, this, [this](bool enabled) {
            if (enabled) {
                ui->view->setFrozenColumns({0});
                ui->view->setFrozenRightColumns({7});
            } else {
                ui->view->clearFrozenColumns();
            }
        });
        connect(ui->spacing, &QCheckBox::toggled, this, [this](bool enabled) {
            // Toggle both axes to inspect frozen boundaries and header alignment with gaps.
            ui->view->setRowSpacing(enabled ? 12 : 0);
            ui->view->setColumnSpacing(enabled ? 12 : 0);
        });
    }

    void applySeparator(int width)
    {
        viv::PaneSeparatorStyle style;
        style.width = width;
        style.color = QColor(QStringLiteral("#3074b5"));
        style.lineStyle = Qt::DashLine;
        // Configure separators for column panes and row panes separately.
        ui->view->setPaneSeparatorStyle(style);
        ui->view->setItemPaneSeparatorStyle(style);
    }
    Model *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_frozen_areas
