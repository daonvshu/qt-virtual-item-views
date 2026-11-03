#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace treetable_spacing {

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
        auto *description = new QLabel(QStringLiteral("14 节点间距与分割线：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_rows = new QSpinBox(this);
        m_rows->setRange(0, 40);
        m_rows->setValue(12);
        m_rows->setPrefix(QStringLiteral("行间距 "));
        controls->addWidget(m_rows);
        m_columns = new QSpinBox(this);
        m_columns->setRange(0, 40);
        m_columns->setValue(12);
        m_columns->setPrefix(QStringLiteral("列间距 "));
        controls->addWidget(m_columns);
        m_lines = new QSpinBox(this);
        m_lines->setRange(1, 6);
        m_lines->setValue(1);
        m_lines->setPrefix(QStringLiteral("线宽 "));
        controls->addWidget(m_lines);
        m_grid = new QCheckBox(QStringLiteral("显示网格"), this);
        m_grid->setChecked(true);
        controls->addWidget(m_grid);
        m_custom = new QCheckBox(QStringLiteral("自定义间距控件"), this);
        m_custom->setChecked(false);
        controls->addWidget(m_custom);
        m_cross = new QCheckBox(QStringLiteral("网格穿过间距"), this);
        m_cross->setChecked(true);
        m_cross->setEnabled(false);
        controls->addWidget(m_cross);
        controls->addStretch();
        layout->addLayout(controls);
        auto *placement = new QHBoxLayout;
        m_placement = new QComboBox(this);
        m_placement->addItem(QStringLiteral("间距中心（默认；零间距回退单线）"));
        m_placement->addItem(QStringLiteral("间距两侧"));
        placement->addWidget(m_placement);
        m_verticalHeaderLines = new QCheckBox(QStringLiteral("竖线穿过水平表头"), this);
        m_verticalHeaderLines->setChecked(true);
        placement->addWidget(m_verticalHeaderLines);
        m_horizontalHeaderLines = new QCheckBox(QStringLiteral("横线穿过垂直表头"), this);
        m_horizontalHeaderLines->setChecked(true);
        placement->addWidget(m_horizontalHeaderLines);
        placement->addStretch();
        layout->addLayout(placement);
        auto *colors = new QHBoxLayout;
        m_bodyColor = new QCheckBox(QStringLiteral("内容间距背景色"), this);
        colors->addWidget(m_bodyColor);
        m_headerColor = new QCheckBox(QStringLiteral("表头间距背景色"), this);
        colors->addWidget(m_headerColor);
        m_stateCross = new QCheckBox(QStringLiteral("Hover / 选中背景穿过列间距"), this);
        m_stateCross->setChecked(true);
        colors->addWidget(m_stateCross);
        m_rowStateCross = new QCheckBox(QStringLiteral("状态延伸到上下半行间距"), this);
        m_rowStateCross->setChecked(true);
        colors->addWidget(m_rowStateCross);
        colors->addStretch();
        layout->addLayout(colors);
        auto *scenarios = new QHBoxLayout;
        m_frozenColumns = new QCheckBox(QStringLiteral("冻结左右列（各一列）"), this);
        scenarios->addWidget(m_frozenColumns);
        m_smallData = new QCheckBox(QStringLiteral("少量数据（2 组 × 3 列）"), this);
        scenarios->addWidget(m_smallData);
        m_nodeSpacing = new QCheckBox(QStringLiteral("节点独立间距（18 + 6）"), this);
        m_nodeSpacing->setToolTip(QStringLiteral("为节点 1.3 设置下间距 18，为节点 1.4 设置上间距 6，相邻间距合计 24，覆盖统一行间距。"));
        scenarios->addWidget(m_nodeSpacing);
        scenarios->addStretch();
        layout->addLayout(scenarios);
        m_view = new viv::VirtualTreeTableView(this);
        // The frame has its own style; use the same color as this chapter's grid.
        m_view->setFrameBorderWidth(1);
        m_view->setFrameBorderColor(QColor(QStringLiteral("#8aa2bb")));
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
        m_model = new QStandardItemModel(m_view);
        populateModel(false);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        // Expand through the view; model row numbers remain local to each parent.
        m_view->setExpansionAnimationEnabled(false);
        m_view->expandRecursively(m_model->index(0, 0));
        m_view->setExpansionAnimationEnabled(true);
        m_view->setColumnWidth(0, 280);
        // Center placement extends each state to the adjacent half of each row gap.
        m_view->setRowSpacing(12);
        m_view->setColumnSpacing(12);
        m_view->setVisualStateThroughColumnSpacing(true);
        m_view->setVisualStateThroughRowSpacing(true);
        // Enable resizing here to demonstrate handles aligned with the spacing grid lines.
        m_view->setColumnResizeEnabled(true);
        m_view->setVerticalHeaderResizeEnabled(true);
        m_view->setHorizontalGridLineColor(QColor(QStringLiteral("#8aa2bb")));
        m_view->setVerticalGridLineColor(QColor(QStringLiteral("#8aa2bb")));
        // One style configures both row and column pane dividers, independently of the grid.
        viv::PaneSeparatorStyle frozenSeparator;
        frozenSeparator.width = 2;
        frozenSeparator.color = QColor(Qt::red);
        frozenSeparator.lineStyle = Qt::DashLine;
        m_view->setPaneSeparatorStyle(frozenSeparator);
        // Center placement always crosses gaps; zero spacing shares one item boundary.
        m_view->setGridLinePlacement(viv::GridLinePlacement::SpacingCenter);
        // Tree-table row lines always include the indentation and branch cells.
        connect(m_placement, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            m_view->setGridLinePlacement(mode == 0 ? viv::GridLinePlacement::SpacingCenter
                                                  : viv::GridLinePlacement::SpacingEdges);
            m_cross->setEnabled(mode != 0);
            m_rowStateCross->setEnabled(mode == 0);
            if (mode == 0) {
                m_cross->setChecked(true);
            } else {
                m_view->setVerticalSpacingLineThroughRowSpacing(m_cross->isChecked());
                m_view->setHorizontalSpacingLineThroughColumnSpacing(m_cross->isChecked());
            }
        });
        connect(m_verticalHeaderLines, &QCheckBox::toggled, m_view,
                &viv::VirtualTreeTableView::setVerticalGridLinesThroughHeader);
        connect(m_horizontalHeaderLines, &QCheckBox::toggled, m_view,
                &viv::VirtualTreeTableView::setHorizontalGridLinesThroughHeader);
        connect(m_frozenColumns, &QCheckBox::toggled, this, [this](bool) {
            applyFrozenColumns();
        });
        connect(m_smallData, &QCheckBox::toggled, this, [this](bool small) {
            // Release old logical column indexes before changing the model dimensions.
            m_view->clearFrozenColumns();
            populateModel(small);
            applyFrozenColumns();
            // Return to the content origin while keeping normal scrollbar visibility rules.
            m_view->scrollByHorizontalPixels(-m_view->horizontalOffset());
            m_view->scrollTo(m_model->index(0, 0), viv::VirtualItemView::PositionAtTop);
        });
        connect(m_nodeSpacing, &QCheckBox::toggled, this, [this](bool) {
            applyNodeSpacing();
        });

        connect(m_bodyColor, &QCheckBox::toggled, this, [this](bool enabled) {
            // Color only body row/column gaps; invalid restores the palette defaults.
            m_view->setSpacingBackgroundColor(enabled ? QColor(QStringLiteral("#e3edf7")) : QColor());
        });
        connect(m_headerColor, &QCheckBox::toggled, this, [this](bool enabled) {
            // Both header axes and frozen header panes share this gap color.
            const QColor color = enabled ? QColor(QStringLiteral("#e3edf7")) : QColor();
            m_view->horizontalHeader()->setSpacingBackgroundColor(color);
            m_view->verticalHeader()->setSpacingBackgroundColor(color);
        });
        connect(m_stateCross, &QCheckBox::toggled, this, [this](bool enabled) {
            // This controls horizontal gap coverage for whole-row hover and selection.
            m_view->setVisualStateThroughColumnSpacing(enabled);
        });
        connect(m_rowStateCross, &QCheckBox::toggled, m_view,
                &viv::VirtualTreeTableView::setVisualStateThroughRowSpacing);
        connect(m_rows, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setRowSpacing(value);
        });
        connect(m_columns, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setColumnSpacing(value);
        });
        connect(m_lines, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setHorizontalGridLineWidth(value);
            m_view->setVerticalGridLineWidth(value);
        });
        connect(m_grid, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setHorizontalGridLinesVisible(enabled);
            m_view->setVerticalGridLinesVisible(enabled);
        });
        connect(m_cross, &QCheckBox::toggled, this, [this](bool enabled) {
            // Only edge placement accepts this option; center placement always crosses gaps.
            m_view->setVerticalSpacingLineThroughRowSpacing(enabled);
            m_view->setHorizontalSpacingLineThroughColumnSpacing(enabled);
        });
        connect(m_custom, &QCheckBox::toggled, this, [this](bool enabled) {
            if (enabled) {
                // The factory only creates widgets; the binder replaces their content on every reuse.
                m_view->setRowSpacingFactory(
                    [](const QModelIndex &, QWidget *parent) -> QWidget * {
                        return new QLabel(parent);
                    },
                    [](QWidget *widget, const QModelIndex &above) {
                        static_cast<QLabel *>(widget)->setText(QStringLiteral("行 %1 下方").arg(above.row() + 1));
                    });
                m_view->setColumnSpacingFactory(
                    [](int, QWidget *parent) -> QWidget * { return new QLabel(parent); },
                    [](QWidget *widget, int column) {
                        static_cast<QLabel *>(widget)->setText(QString::number(column + 1));
                    });
                // Header gaps use a separate factory and may show different content from body gaps.
                m_view->setHeaderColumnSpacingFactory(
                    [](int, QWidget *parent) -> QWidget * { return new QLabel(parent); },
                    [](QWidget *widget, int) {
                        static_cast<QLabel *>(widget)->setText(QStringLiteral("·"));
                    });
            } else {
                m_view->setRowSpacingFactory({}, {});
                m_view->setColumnSpacingFactory({}, {});
                m_view->setHeaderColumnSpacingFactory({}, {});
            }
        });
    }

    void populateModel(bool small)
    {
        // Regenerate removed cells as well when returning to the larger data set.
        m_model->clear();
        m_model->setRowCount(small ? 2 : 8);
        m_model->setColumnCount(small ? 3 : 8);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column),
                    QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        appendChildren(m_model);
        applyNodeSpacing();
        if (m_view->model() == m_model) m_view->expand(m_model->index(0, 0));
    }

    void applyNodeSpacing()
    {
        // Per-node overrides live on column zero; visible row numbers change with expansion.
        // The next node's above gap is added to this node's below gap (18 + 6 = 24).
        // Clear both roles when disabled to restore rowSpacing() with no added above gap.
        const bool enabled = m_nodeSpacing->isChecked();
        const QModelIndex group = m_model->index(0, 0);
        m_model->setData(m_model->index(2, 0, group), enabled ? QVariant(18) : QVariant(),
                         viv::VirtualTreeTableView::NodeRowSpacingBelowRole);
        m_model->setData(m_model->index(3, 0, group), enabled ? QVariant(6) : QVariant(),
                         viv::VirtualTreeTableView::NodeRowSpacingAboveRole);
    }

    void applyFrozenColumns()
    {
        // Recompute the last logical index for both data sizes; the middle stays scrollable.
        if (m_frozenColumns->isChecked()) {
            m_view->setFrozenColumns({0});
            m_view->setFrozenRightColumns({m_model->columnCount() - 1});
        } else {
            m_view->clearFrozenColumns();
        }
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QSpinBox *m_rows = nullptr;
    QSpinBox *m_columns = nullptr;
    QSpinBox *m_lines = nullptr;
    QCheckBox *m_grid = nullptr;
    QCheckBox *m_custom = nullptr;
    QCheckBox *m_cross = nullptr;
    QComboBox *m_placement = nullptr;
    QCheckBox *m_verticalHeaderLines = nullptr;
    QCheckBox *m_horizontalHeaderLines = nullptr;
    QCheckBox *m_bodyColor = nullptr;
    QCheckBox *m_headerColor = nullptr;
    QCheckBox *m_stateCross = nullptr;
    QCheckBox *m_rowStateCross = nullptr;
    QCheckBox *m_frozenColumns = nullptr;
    QCheckBox *m_smallData = nullptr;
    QCheckBox *m_nodeSpacing = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_spacing
