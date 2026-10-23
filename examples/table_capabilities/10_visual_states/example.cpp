#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace table_visual_states {

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
        auto *description = new QLabel(QStringLiteral("10 Hover 与选中背景：背景跟随按行或按格选择，可配置颜色、圆角、时长和曲线"), this);
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
        // Backgrounds are enabled by default; keep content widgets transparent without custom painting.
        m_view->setVisualStateBackgroundVisible(true);
        m_view->setVisualStateCornerRadius(8);
        m_view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        m_view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // Hover and selected backgrounds both follow the selection behavior.
        m_view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);

        connect(m_behavior, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->selectionModel()->clearSelection();
            m_view->setSelectionBehavior(value == 0 ? viv::VirtualItemView::SelectionBehavior::SelectRows
                                                   : viv::VirtualItemView::SelectionBehavior::SelectItems);
        });
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

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QComboBox *m_behavior = nullptr;
    QSpinBox *m_radius = nullptr;
    QSpinBox *m_duration = nullptr;
    QComboBox *m_curve = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_visual_states
