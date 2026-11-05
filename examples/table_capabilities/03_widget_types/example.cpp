#include "ui_widget_types.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QProgressBar>

namespace table_widget_types {

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    viv::WidgetType cellWidgetType(const QModelIndex &index) const override { return index.column() == 1 ? 1 : 0; }
    QWidget *createCellWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1) {
            auto *bar = new QProgressBar(parent);
            bar->setRange(0, 100);
            return bar;
        }
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (cellWidgetType(index) == 1)
            static_cast<QProgressBar *>(widget)->setValue(index.row() % 101);
        else
            static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        if (auto *bar = qobject_cast<QProgressBar *>(widget))
            bar->setValue(0);
        else
            static_cast<QLabel *>(widget)->clear();
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableWidgetTypesPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableWidgetTypesPage> ui;

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
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_widget_types
