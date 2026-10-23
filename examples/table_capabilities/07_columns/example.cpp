#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace table_columns {

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
        auto *description = new QLabel(QStringLiteral("07 列宽、隐藏与换序：拖动列边界调整宽度，列换序保持模型逻辑列不变"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_width = new QSpinBox(this);
        m_width->setRange(60, 400);
        m_width->setValue(150);
        m_width->setPrefix(QStringLiteral("首列宽 "));
        controls->addWidget(m_width);
        m_hide = new QCheckBox(QStringLiteral("隐藏第 2 列"), this);
        m_hide->setChecked(false);
        controls->addWidget(m_hide);
        m_move = new QPushButton(QStringLiteral("第 3 列移到首列"), this);
        controls->addWidget(m_move);
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
        m_model = new QStandardItemModel(120, 8, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        // Minimum and maximum widths constrain both interactive and programmatic resizing.
        m_view->setColumnMinimumWidth(60);
        m_view->setColumnMaximumWidth(400);
        // Dragging column headers changes visual order without moving model columns.
        m_view->setColumnDragEnabled(true);
        m_view->setColumnResizeEnabled(true);

        connect(m_width, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setColumnWidth(0, value);
        });
        connect(m_hide, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setColumnHidden(1, enabled);
        });
        connect(m_move, &QPushButton::clicked, this, [this] {
            m_view->moveColumn(2, 0, viv::VirtualTableView::MoveAnimation::Animate);
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QSpinBox *m_width = nullptr;
    QCheckBox *m_hide = nullptr;
    QPushButton *m_move = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_columns
