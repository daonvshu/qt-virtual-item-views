#include "ui_header_persistence.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>

namespace table_header_persistence {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableHeaderPersistencePage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableHeaderPersistencePage> ui;

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
        // Applications may persist this initial snapshot; this example keeps it in memory.
        m_saved = ui->view->saveHeaderState();
        ui->view->setColumnDragEnabled(true);
        // Allow manual column widths to be included in the saved header layout.
        ui->view->setColumnResizeEnabled(true);

        connect(ui->save, &QPushButton::clicked, this, [this] {
            m_saved = ui->view->saveHeaderState();
            ui->status->setText(QStringLiteral("已保存 %1 字节").arg(m_saved.size()));
        });
        connect(ui->change, &QPushButton::clicked, this, [this] {
            ui->view->setColumnWidth(0, 240);
            ui->view->setColumnHidden(2, true);
            ui->view->moveColumn(3, 0);
            ui->view->setFrozenColumns({0});
            ui->view->setFrozenRows(1);
        });
        connect(ui->restore, &QPushButton::clicked, this, [this] {
            const bool restored = ui->view->restoreHeaderState(m_saved);
            ui->status->setText(restored ? QStringLiteral("布局恢复成功") : QStringLiteral("布局数据无效，保留当前布局"));
        });
    }

    QStandardItemModel *m_model = nullptr;
    QByteArray m_saved;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_header_persistence
