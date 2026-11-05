#include "ui_smooth_scrolling.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableSmoothScrollingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableSmoothScrollingPage> ui;

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
        m_model = new QStandardItemModel(1000, 24, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // Use one view-owned controller per table; both scroll directions use the existing view geometry.
        m_controller = new sscroll::SmoothScrollController(ui->view, ui->view);
        ui->view->setFrozenColumns({0});
        applyStep(48);

        connect(ui->enabled, &QCheckBox::toggled, this, [this](bool enabled) {
            m_controller->setEnabled(enabled);
        });
        connect(ui->step, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            applyStep(value);
        });
        connect(ui->stop, &QPushButton::clicked, this, [this] {
            m_controller->stop();
        });
    }

    void applyStep(int pixels)
    {
        // Native wheel scrolling uses the same step when the controller is disabled.
        ui->view->setWheelScrollPixels(pixels);
        ui->view->setHorizontalWheelPixels(pixels);
        auto settings = m_controller->settings();
        settings.wheelStep = pixels;
        settings.minimumWheelStep = 1;
        m_controller->setSettings(settings);
    }
    QStandardItemModel *m_model = nullptr;
    sscroll::SmoothScrollController *m_controller = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_smooth_scrolling
