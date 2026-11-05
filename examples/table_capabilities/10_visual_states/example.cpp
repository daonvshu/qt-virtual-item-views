#include "ui_visual_states.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableVisualStatesPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableVisualStatesPage> ui;

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
        m_model = new QStandardItemModel(120, 8, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // Backgrounds are enabled by default; keep content widgets transparent without custom painting.
        ui->view->setVisualStateBackgroundVisible(true);
        ui->view->setVisualStateCornerRadius(8);
        ui->view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        ui->view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // Hover and selected backgrounds both follow the selection behavior.
        ui->view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);

        connect(ui->behavior, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->selectionModel()->clearSelection();
            ui->view->setSelectionBehavior(value == 0 ? viv::VirtualItemView::SelectionBehavior::SelectRows
                                                   : viv::VirtualItemView::SelectionBehavior::SelectItems);
        });
        connect(ui->radius, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setVisualStateCornerRadius(value);
        });
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setVisualStateAnimationDuration(value);
        });
        connect(ui->curve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            const QEasingCurve::Type curves[] = {QEasingCurve::OutCubic, QEasingCurve::InOutCubic, QEasingCurve::Linear};
            ui->view->setVisualStateAnimationEasingCurve(QEasingCurve(curves[value]));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_visual_states
