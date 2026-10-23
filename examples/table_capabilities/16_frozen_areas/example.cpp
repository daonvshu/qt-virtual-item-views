#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/reorderabletablemodel.h>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("16 冻结行列与分隔线：左右冻结列与上下冻结行组合，滚动内容在各自区域裁剪"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_top = new QSpinBox(this);
        m_top->setRange(0, 8);
        m_top->setValue(2);
        m_top->setPrefix(QStringLiteral("顶部行 "));
        controls->addWidget(m_top);
        m_bottom = new QSpinBox(this);
        m_bottom->setRange(0, 8);
        m_bottom->setValue(1);
        m_bottom->setPrefix(QStringLiteral("底部行 "));
        controls->addWidget(m_bottom);
        m_width = new QSpinBox(this);
        m_width->setRange(0, 6);
        m_width->setValue(2);
        m_width->setPrefix(QStringLiteral("分隔线宽 "));
        controls->addWidget(m_width);
        m_columns = new QCheckBox(QStringLiteral("冻结左右列"), this);
        m_columns->setChecked(true);
        controls->addWidget(m_columns);
        m_spacing = new QCheckBox(QStringLiteral("显示间距"), this);
        controls->addWidget(m_spacing);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new Model(m_view);
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        // Header dragging reorders columns; row dragging needs the model's moveRows support.
        m_view->setColumnDragEnabled(true);
        m_view->setVerticalHeaderDragEnabled(true);
        // Section edges resize column widths and row heights, including frozen header panes.
        m_view->setColumnResizeEnabled(true);
        m_view->setVerticalHeaderResizeEnabled(true);
        // Frozen column sets use logical indexes and are unaffected by visual reordering.
        m_view->setFrozenColumns({0, 1});
        m_view->setFrozenRightColumns({6,7});
        m_view->setFrozenRows(2);
        m_view->setFrozenBottomRows(1);
        applySeparator(2);

        connect(m_top, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setFrozenRows(value);
        });
        connect(m_bottom, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setFrozenBottomRows(value);
        });
        connect(m_width, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            applySeparator(value);
        });
        connect(m_columns, &QCheckBox::toggled, this, [this](bool enabled) {
            if (enabled) {
                m_view->setFrozenColumns({0});
                m_view->setFrozenRightColumns({7});
            } else {
                m_view->clearFrozenColumns();
            }
        });
        connect(m_spacing, &QCheckBox::toggled, this, [this](bool enabled) {
            // Toggle both axes to inspect frozen boundaries and header alignment with gaps.
            m_view->setRowSpacing(enabled ? 12 : 0);
            m_view->setColumnSpacing(enabled ? 12 : 0);
        });
    }

    void applySeparator(int width)
    {
        viv::PaneSeparatorStyle style;
        style.width = width;
        style.color = QColor(QStringLiteral("#3074b5"));
        style.lineStyle = Qt::DashLine;
        // Configure separators for column panes and row panes separately.
        m_view->setPaneSeparatorStyle(style);
        m_view->setItemPaneSeparatorStyle(style);
    }
    viv::VirtualTableView *m_view = nullptr;
    Model *m_model = nullptr;
    QSpinBox *m_top = nullptr;
    QSpinBox *m_bottom = nullptr;
    QSpinBox *m_width = nullptr;
    QCheckBox *m_columns = nullptr;
    QCheckBox *m_spacing = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_frozen_areas
