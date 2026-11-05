#include "ui_panes.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

namespace table_panes {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TablePanesPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TablePanesPage> ui;

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
        m_model = new QStandardItemModel(100, 12, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        viv::TablePaneSpec left;
        left.logicalColumns = {0};
        left.scroll = viv::PaneScroll::Frozen;
        viv::TablePaneSpec first;
        first.logicalColumns = {1, 2, 3, 4, 5};
        first.scrollGroup = 0;
        viv::TablePaneSpec second;
        second.logicalColumns = {6, 7, 8, 9, 10, 11};
        second.scrollGroup = 1;
        // The main scrollbar drives the first scrollable pane; use the group overload for other groups.
        // Horizontal wheel input and Shift+wheel automatically target the pane under the pointer.
        ui->view->setPanes({left, first, second});

        connect(ui->group0, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setHorizontalOffset(0, value);
        });
        connect(ui->group1, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setHorizontalOffset(1, value);
        });
        connect(ui->query, &QPushButton::clicked, this, [this] {
            ui->status->setText(QStringLiteral("pane=%1，滚动组=%2，主组=%3").arg(ui->view->paneSpecs().size()).arg(ui->view->scrollGroups().size()).arg(ui->view->primaryScrollGroup()));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_panes
