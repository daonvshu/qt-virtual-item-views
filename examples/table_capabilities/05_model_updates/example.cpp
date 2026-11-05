#include "ui_model_updates.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>

namespace table_model_updates {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableModelUpdatesPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableModelUpdatesPage> ui;

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

        connect(ui->edit, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            if (index.isValid()) m_model->setData(index, QStringLiteral("内容已更新"));
        });
        connect(ui->add, &QPushButton::clicked, this, [this] {
            m_model->insertRow(0);
            m_model->setData(m_model->index(0, 0), QStringLiteral("新行"));
        });
        connect(ui->remove, &QPushButton::clicked, this, [this] {
            const int row = ui->view->currentIndex().row();
            if (row >= 0) m_model->removeRow(row);
        });
        connect(ui->column, &QPushButton::clicked, this, [this] {
            const int c = m_model->columnCount();
            m_model->insertColumn(c);
            m_model->setHeaderData(c, Qt::Horizontal, QStringLiteral("新增列"));
        });
        connect(ui->reset, &QPushButton::clicked, this, [this] {
            m_model->clear();
            m_model->setColumnCount(8);
            m_model->setRowCount(40);
            m_model->setData(m_model->index(0, 0), QStringLiteral("重置后的数据"));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_model_updates
