#include "ui_stretch_columns.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/headergeometry.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSignalBlocker>

namespace table_stretch_columns {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableStretchColumnsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableStretchColumnsPage> ui;

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
        // Enable edge dragging to demonstrate proportional and viewport-fitting resize modes.
        ui->view->setColumnResizeEnabled(true);
        m_model = new QStandardItemModel(80, 4, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        applyStretch();

        connect(ui->apply, &QPushButton::clicked, this, [this] {
            ui->last->setChecked(false);
            applyStretch();
        });
        connect(ui->fixedWidth, &QPushButton::clicked, this, [this] {
            // Independent mode leaves proportional sizing. FitViewport keeps fitting and
            // compensates with columns on the right (the left for the last visible column).
            ui->view->setColumnWidth(2, 200);
        });
        connect(ui->last, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setColumnResizeMode(viv::VirtualTableView::ColumnResizeMode::Independent);
            // Clear proportional factors before demonstrating last-column stretching.
            for (int column = 1; column < 4; ++column) {
                ui->view->setColumnStretchFactor(column, 0);
                ui->view->setColumnWidth(column, 150);
            }
            ui->view->setStretchLastColumn(enabled);
            if (!enabled)
                applyStretch();
        });
        connect(ui->view->horizontalHeaderGeometry(), &viv::HeaderGeometry::stretchLastSectionChanged,
                this, [this](bool enabled) {
            // A manual resize can disable last-column stretching. Reflect that change
            // without running the mode-switch handler and resetting column widths.
            const QSignalBlocker blocker(ui->last);
            ui->last->setChecked(enabled);
        });
        connect(ui->fit, &QCheckBox::toggled, this, [this](bool enabled) {
            // FitViewport captures current widths as proportions. Dragging either edge
            // changes that column and distributes the opposite delta on the chosen side.
            // Horizontal content fits exactly, including configured column spacing.
            using Mode = viv::VirtualTableView::ColumnResizeMode;
            ui->view->setColumnResizeMode(enabled ? Mode::FitViewport : Mode::Independent);
        });
        connect(ui->view->horizontalHeaderGeometry(), &viv::HeaderGeometry::resizeModeChanged,
                this, [this](viv::HeaderGeometry::ResizeMode mode) {
            // Restoring ordinary factors or last-column stretching also exits fit mode.
            const QSignalBlocker blocker(ui->fit);
            ui->fit->setChecked(mode == viv::HeaderGeometry::ResizeMode::FitExtent);
        });
    }

    void applyStretch()
    {
        // Positive factors explicitly restore proportional sizing after a manual resize.
        // Set fixed widths first: resizing any column would clear all factors again.
        ui->view->setColumnResizeMode(viv::VirtualTableView::ColumnResizeMode::Independent);
        ui->view->setColumnWidth(0, 120);
        ui->view->setColumnStretchFactor(0, 0);
        ui->view->setColumnStretchFactor(1, 1);
        ui->view->setColumnStretchFactor(2, 2);
        ui->view->setColumnStretchFactor(3, 1);
    }
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_stretch_columns
