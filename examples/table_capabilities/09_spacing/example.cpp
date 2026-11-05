#include "ui_spacing.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>

namespace table_spacing {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableSpacingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableSpacingPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
        ui->view->setObjectName(QStringLiteral("spacingTable"));
        // Match the grid color; target only the table's outer frame.
        ui->view->setStyleSheet(QStringLiteral("#spacingTable { border: 1px solid #8aa2bb; }"));
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
        m_model = new QStandardItemModel(ui->view);
        populateModel(false);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // Center placement extends each state to the adjacent half of each row gap.
        ui->view->setRowSpacing(12);
        ui->view->setColumnSpacing(12);
        // The orientation names the header, not the direction of the gap.
        // Default false: keep outer half-spacing in center mode, zero in edge mode.
        ui->view->setHeaderContentSpacingEnabled(Qt::Horizontal, false);
        ui->view->setHeaderContentSpacingEnabled(Qt::Vertical, false);
        connect(ui->horizontalHeaderGap, &QCheckBox::toggled, this, [this](bool enabled) {
            // Horizontal header: add the full rowSpacing() above the first row.
            ui->view->setHeaderContentSpacingEnabled(Qt::Horizontal, enabled);
        });
        connect(ui->verticalHeaderGap, &QCheckBox::toggled, this, [this](bool enabled) {
            // Vertical header: add the full columnSpacing() before the first column.
            ui->view->setHeaderContentSpacingEnabled(Qt::Vertical, enabled);
        });
        ui->view->setVisualStateThroughColumnSpacing(true);
        ui->view->setVisualStateThroughRowSpacing(true);
        // Enable resizing here to demonstrate handles aligned with the spacing grid lines.
        ui->view->setColumnResizeEnabled(true);
        ui->view->setVerticalHeaderResizeEnabled(true);
        ui->view->setHorizontalGridLineColor(QColor(QStringLiteral("#8aa2bb")));
        ui->view->setVerticalGridLineColor(QColor(QStringLiteral("#8aa2bb")));
        // One style configures both row and column pane dividers, independently of the grid.
        viv::PaneSeparatorStyle frozenSeparator;
        frozenSeparator.width = 2;
        frozenSeparator.color = QColor(Qt::red);
        frozenSeparator.lineStyle = Qt::DashLine;
        ui->view->setPaneSeparatorStyle(frozenSeparator);
        // Center placement always crosses gaps; zero spacing shares one item boundary.
        ui->view->setGridLinePlacement(viv::GridLinePlacement::SpacingCenter);
        connect(ui->placement, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setGridLinePlacement(mode == 0 ? viv::GridLinePlacement::SpacingCenter
                                                  : viv::GridLinePlacement::SpacingEdges);
            ui->cross->setEnabled(mode != 0);
            ui->rowStateCross->setEnabled(mode == 0);
            if (mode == 0) {
                ui->cross->setChecked(true);
            } else {
                ui->view->setVerticalSpacingLineThroughRowSpacing(ui->cross->isChecked());
                ui->view->setHorizontalSpacingLineThroughColumnSpacing(ui->cross->isChecked());
            }
        });
        connect(ui->verticalHeaderLines, &QCheckBox::toggled, ui->view,
                &viv::VirtualTableView::setVerticalGridLinesThroughHeader);
        connect(ui->horizontalHeaderLines, &QCheckBox::toggled, ui->view,
                &viv::VirtualTableView::setHorizontalGridLinesThroughHeader);
        connect(ui->frozenColumns, &QCheckBox::toggled, this, [this](bool) {
            applyFrozenColumns();
        });
        connect(ui->frozenRows, &QCheckBox::toggled, this, [this](bool enabled) {
            // Both placements split pane gaps; edge placement also outlines each cell side.
            ui->view->setFrozenRows(enabled ? 1 : 0);
            ui->view->setFrozenBottomRows(enabled ? 1 : 0);
        });
        connect(ui->smallData, &QCheckBox::toggled, this, [this](bool small) {
            // Release old logical column indexes before changing the model dimensions.
            ui->view->clearFrozenColumns();
            populateModel(small);
            applyFrozenColumns();
            // Return to the content origin while keeping normal scrollbar visibility rules.
            ui->view->scrollByHorizontalPixels(-ui->view->horizontalOffset());
            ui->view->scrollTo(m_model->index(0, 0), viv::VirtualItemView::PositionAtTop);
        });

        connect(ui->bodyColor, &QCheckBox::toggled, this, [this](bool enabled) {
            // Color only body row/column gaps; invalid restores the palette defaults.
            ui->view->setSpacingBackgroundColor(enabled ? QColor(QStringLiteral("#e3edf7")) : QColor());
        });
        connect(ui->headerColor, &QCheckBox::toggled, this, [this](bool enabled) {
            // Both header axes and frozen header panes share this gap color.
            const QColor color = enabled ? QColor(QStringLiteral("#e3edf7")) : QColor();
            ui->view->horizontalHeader()->setSpacingBackgroundColor(color);
            ui->view->verticalHeader()->setSpacingBackgroundColor(color);
        });
        connect(ui->stateCross, &QCheckBox::toggled, this, [this](bool enabled) {
            // This controls horizontal gap coverage for whole-row hover and selection.
            ui->view->setVisualStateThroughColumnSpacing(enabled);
        });
        connect(ui->rowStateCross, &QCheckBox::toggled, ui->view,
                &viv::VirtualTableView::setVisualStateThroughRowSpacing);
        connect(ui->rows, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setRowSpacing(value);
        });
        connect(ui->columns, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setColumnSpacing(value);
        });
        connect(ui->lines, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setHorizontalGridLineWidth(value);
            ui->view->setVerticalGridLineWidth(value);
        });
        connect(ui->grid, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setHorizontalGridLinesVisible(enabled);
            ui->view->setVerticalGridLinesVisible(enabled);
        });
        connect(ui->cross, &QCheckBox::toggled, this, [this](bool enabled) {
            // Only edge placement accepts this option; center placement always crosses gaps.
            ui->view->setVerticalSpacingLineThroughRowSpacing(enabled);
            ui->view->setHorizontalSpacingLineThroughColumnSpacing(enabled);
        });
        connect(ui->custom, &QCheckBox::toggled, this, [this](bool enabled) {
            if (enabled) {
                // The factory only creates widgets; the binder replaces their content on every reuse.
                ui->view->setRowSpacingFactory(
                    [](const QModelIndex &, QWidget *parent) -> QWidget * {
                        return new QLabel(parent);
                    },
                    [](QWidget *widget, const QModelIndex &above) {
                        static_cast<QLabel *>(widget)->setText(QStringLiteral("行 %1 下方").arg(above.row() + 1));
                    });
                ui->view->setColumnSpacingFactory(
                    [](int, QWidget *parent) -> QWidget * { return new QLabel(parent); },
                    [](QWidget *widget, int column) {
                        static_cast<QLabel *>(widget)->setText(QString::number(column + 1));
                    });
                // Header gaps use a separate factory and may show different content from body gaps.
                ui->view->setHeaderColumnSpacingFactory(
                    [](int, QWidget *parent) -> QWidget * { return new QLabel(parent); },
                    [](QWidget *widget, int) {
                        static_cast<QLabel *>(widget)->setText(QStringLiteral("·"));
                    });
            } else {
                ui->view->setRowSpacingFactory({}, {});
                ui->view->setColumnSpacingFactory({}, {});
                ui->view->setHeaderColumnSpacingFactory({}, {});
            }
        });
    }

    void populateModel(bool small)
    {
        // Regenerate removed cells as well when returning to the larger data set.
        m_model->setRowCount(small ? 4 : 120);
        m_model->setColumnCount(small ? 3 : 8);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column),
                    QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
    }

    void applyFrozenColumns()
    {
        // Recompute the last logical index for both data sizes; the middle stays scrollable.
        if (ui->frozenColumns->isChecked()) {
            ui->view->setFrozenColumns({0});
            ui->view->setFrozenRightColumns({m_model->columnCount() - 1});
        } else {
            ui->view->clearFrozenColumns();
        }
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_spacing
