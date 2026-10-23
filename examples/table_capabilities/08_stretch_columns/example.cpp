#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/headergeometry.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("08 列宽模式：普通模式下任意列调宽后固定全部列；填满视图模式保持总宽不变，由拖动侧的列按现有宽度比例分摊"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_apply = new QPushButton(QStringLiteral("恢复 1:2:1 比例"), this);
        controls->addWidget(m_apply);
        m_fixedWidth = new QPushButton(QStringLiteral("设置列 3 宽度为 200"), this);
        controls->addWidget(m_fixedWidth);
        m_last = new QCheckBox(QStringLiteral("拉伸末列"), this);
        m_last->setChecked(false);
        controls->addWidget(m_last);
        m_fit = new QCheckBox(QStringLiteral("填满视图（两侧比例调宽）"), this);
        m_fit->setToolTip(QStringLiteral("在目标列内靠近左边界拖动，由左侧列分摊；靠近右边界拖动，由右侧列分摊。最外侧边界固定。"));
        controls->addWidget(m_fit);
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
        // Enable edge dragging to demonstrate proportional and viewport-fitting resize modes.
        m_view->setColumnResizeEnabled(true);
        m_model = new QStandardItemModel(80, 4, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        applyStretch();

        connect(m_apply, &QPushButton::clicked, this, [this] {
            m_last->setChecked(false);
            applyStretch();
        });
        connect(m_fixedWidth, &QPushButton::clicked, this, [this] {
            // Independent mode leaves proportional sizing. FitViewport keeps fitting and
            // compensates with columns on the right (the left for the last visible column).
            m_view->setColumnWidth(2, 200);
        });
        connect(m_last, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setColumnResizeMode(viv::VirtualTableView::ColumnResizeMode::Independent);
            // Clear proportional factors before demonstrating last-column stretching.
            for (int column = 1; column < 4; ++column) {
                m_view->setColumnStretchFactor(column, 0);
                m_view->setColumnWidth(column, 150);
            }
            m_view->setStretchLastColumn(enabled);
            if (!enabled)
                applyStretch();
        });
        connect(m_view->horizontalHeaderGeometry(), &viv::HeaderGeometry::stretchLastSectionChanged,
                this, [this](bool enabled) {
            // A manual resize can disable last-column stretching. Reflect that change
            // without running the mode-switch handler and resetting column widths.
            const QSignalBlocker blocker(m_last);
            m_last->setChecked(enabled);
        });
        connect(m_fit, &QCheckBox::toggled, this, [this](bool enabled) {
            // FitViewport captures current widths as proportions. Dragging either edge
            // changes that column and distributes the opposite delta on the chosen side.
            // Horizontal content fits exactly, including configured column spacing.
            using Mode = viv::VirtualTableView::ColumnResizeMode;
            m_view->setColumnResizeMode(enabled ? Mode::FitViewport : Mode::Independent);
        });
        connect(m_view->horizontalHeaderGeometry(), &viv::HeaderGeometry::resizeModeChanged,
                this, [this](viv::HeaderGeometry::ResizeMode mode) {
            // Restoring ordinary factors or last-column stretching also exits fit mode.
            const QSignalBlocker blocker(m_fit);
            m_fit->setChecked(mode == viv::HeaderGeometry::ResizeMode::FitExtent);
        });
    }

    void applyStretch()
    {
        // Positive factors explicitly restore proportional sizing after a manual resize.
        // Set fixed widths first: resizing any column would clear all factors again.
        m_view->setColumnResizeMode(viv::VirtualTableView::ColumnResizeMode::Independent);
        m_view->setColumnWidth(0, 120);
        m_view->setColumnStretchFactor(0, 0);
        m_view->setColumnStretchFactor(1, 1);
        m_view->setColumnStretchFactor(2, 2);
        m_view->setColumnStretchFactor(3, 1);
    }
    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QPushButton *m_apply = nullptr;
    QPushButton *m_fixedWidth = nullptr;
    QCheckBox *m_last = nullptr;
    QCheckBox *m_fit = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_stretch_columns
