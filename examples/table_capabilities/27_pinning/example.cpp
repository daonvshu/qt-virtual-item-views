#include "ui_pinning.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QPersistentModelIndex>

namespace table_pinning {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TablePinningPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TablePinningPage> ui;

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
        // Warn above 20 pinned items; this soft limit does not release widgets automatically.
        ui->view->setMaxPinnedItems(20);

        connect(ui->pin, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            if (!index.isValid()) return;
            if (m_pinned.isValid()) ui->view->setItemPinned(m_pinned, false);
            m_pinned = index;
            // CellWidgets pins the individual cell; RowWidgets pins the entire row.
            ui->view->setItemPinned(index, true);
            ui->status->setText(QStringLiteral("已保留 (%1,%2)，可滚出屏幕后查询").arg(index.row()).arg(index.column()));
        });
        connect(ui->query, &QPushButton::clicked, this, [this] {
            ui->status->setText(QStringLiteral("index 有效=%1，widget 存在=%2").arg(m_pinned.isValid()).arg(ui->view->cellWidget(m_pinned) != nullptr));
        });
        connect(ui->release, &QPushButton::clicked, this, [this] {
            if (m_pinned.isValid()) ui->view->setItemPinned(m_pinned, false);
            m_pinned = QPersistentModelIndex();
        });
    }

    QStandardItemModel *m_model = nullptr;
    QPersistentModelIndex m_pinned;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_pinning
