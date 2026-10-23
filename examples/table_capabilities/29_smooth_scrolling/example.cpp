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
#include <smoothscroll/smoothscrollcontroller.h>

namespace table_smooth_scrolling {

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
        auto *description = new QLabel(QStringLiteral("29 平滑滚动（可选）：检测到匹配的 SmoothScrollbar 后才编译和显示，开关对比原生双向滚动"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_enabled = new QCheckBox(QStringLiteral("平滑滚动"), this);
        m_enabled->setChecked(true);
        controls->addWidget(m_enabled);
        m_step = new QSpinBox(this);
        m_step->setRange(1, 240);
        m_step->setValue(48);
        m_step->setPrefix(QStringLiteral("滚轮步长 "));
        controls->addWidget(m_step);
        m_stop = new QPushButton(QStringLiteral("停止动画"), this);
        controls->addWidget(m_stop);
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
        m_model = new QStandardItemModel(1000, 24, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        // Use one view-owned controller per table; both scroll directions use the existing view geometry.
        m_controller = new sscroll::SmoothScrollController(m_view, m_view);
        m_view->setFrozenColumns({0});
        applyStep(48);

        connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) {
            m_controller->setEnabled(enabled);
        });
        connect(m_step, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            applyStep(value);
        });
        connect(m_stop, &QPushButton::clicked, this, [this] {
            m_controller->stop();
        });
    }

    void applyStep(int pixels)
    {
        // Native wheel scrolling uses the same step when the controller is disabled.
        m_view->setWheelScrollPixels(pixels);
        m_view->setHorizontalWheelPixels(pixels);
        auto settings = m_controller->settings();
        settings.wheelStep = pixels;
        settings.minimumWheelStep = 1;
        m_controller->setSettings(settings);
    }
    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QCheckBox *m_enabled = nullptr;
    QSpinBox *m_step = nullptr;
    QPushButton *m_stop = nullptr;
    sscroll::SmoothScrollController *m_controller = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_smooth_scrolling
