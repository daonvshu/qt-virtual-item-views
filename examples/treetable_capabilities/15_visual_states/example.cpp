#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace treetable_visual_states {

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
        auto *description = new QLabel(QStringLiteral("15 状态背景与圆角：整行状态包含分支和缩进区域；按格选择时高亮完整单元格。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_behavior = new QComboBox(this);
        m_behavior->addItems({QStringLiteral("按行选择与高亮"), QStringLiteral("按格选择与高亮")});
        controls->addWidget(m_behavior);
        m_radius = new QSpinBox(this);
        m_radius->setRange(0, 24);
        m_radius->setValue(8);
        m_radius->setPrefix(QStringLiteral("圆角 "));
        controls->addWidget(m_radius);
        m_duration = new QSpinBox(this);
        m_duration->setRange(0, 800);
        m_duration->setValue(180);
        m_duration->setPrefix(QStringLiteral("过渡 ms "));
        controls->addWidget(m_duration);
        m_curve = new QComboBox(this);
        m_curve->addItems({QStringLiteral("OutCubic"), QStringLiteral("InOutCubic"), QStringLiteral("Linear")});
        controls->addWidget(m_curve);
        controls->addStretch();
        layout->addLayout(controls);
        auto *headerOptions = new QHBoxLayout;
        m_headerState = new QCheckBox(QStringLiteral("状态背景延伸至表头"), this);
        m_headerState->setChecked(true);
        headerOptions->addWidget(m_headerState);
        m_headerRadius = new QCheckBox(QStringLiteral("圆角延伸至表头"), this);
        m_headerRadius->setChecked(true);
        headerOptions->addWidget(m_headerRadius);
        headerOptions->addStretch();
        layout->addLayout(headerOptions);
        m_view = new viv::VirtualTreeTableView(this);
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
        m_model = new QStandardItemModel(8, 8, m_view);
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
        // Backgrounds are enabled by default; keep content widgets transparent without custom painting.
        m_view->setVisualStateBackgroundVisible(true);
        m_view->setVisualStateCornerRadius(8);
        m_view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        m_view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // Hover and selected backgrounds both follow the selection behavior.
        // Tree decoration never cuts a hole in a row or cell state background.
        m_view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);

        connect(m_behavior, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->selectionModel()->clearSelection();
            m_view->setSelectionBehavior(value == 0 ? viv::VirtualItemView::SelectionBehavior::SelectRows
                                                   : viv::VirtualItemView::SelectionBehavior::SelectItems);
        });
        connect(m_headerState, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setHeaderVisualStateBackgroundVisible(Qt::Horizontal, enabled);
            m_view->setHeaderVisualStateBackgroundVisible(Qt::Vertical, enabled);
        });
        connect(m_headerRadius, &QCheckBox::toggled, m_view,
                &viv::VirtualTreeTableView::setVisualStateCornerRadiusIncludesHeaders);
        connect(m_radius, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setVisualStateCornerRadius(value);
        });
        connect(m_duration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setVisualStateAnimationDuration(value);
        });
        connect(m_curve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            const QEasingCurve::Type curves[] = {QEasingCurve::OutCubic, QEasingCurve::InOutCubic, QEasingCurve::Linear};
            m_view->setVisualStateAnimationEasingCurve(QEasingCurve(curves[value]));
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QComboBox *m_behavior = nullptr;
    QCheckBox *m_headerState = nullptr;
    QCheckBox *m_headerRadius = nullptr;
    QSpinBox *m_radius = nullptr;
    QSpinBox *m_duration = nullptr;
    QComboBox *m_curve = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_visual_states
