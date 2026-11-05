#include "ui_sorting.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSortFilterProxyModel>

namespace table_sorting {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableSortingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableSortingPage> ui;

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
        m_model->setHeaderData(0, Qt::Horizontal, QStringLiteral("条目名称"));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            const QString rowName = QStringLiteral("条目 %1").arg(row + 1);
            for (int column = 0; column < m_model->columnCount(); ++column) {
                // Keep the filter column free of shared text such as "column 1".
                const QString text = column == 0 ? rowName
                    : QStringLiteral("%1 / 列 %2").arg(rowName).arg(column + 1);
                m_model->setData(m_model->index(row, column), text);
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        m_proxy = new QSortFilterProxyModel(ui->view);
        m_proxy->setSourceModel(m_model);
        m_proxy->setDynamicSortFilter(true);
        // Filter the displayed row name in the first column only.
        m_proxy->setFilterKeyColumn(0);
        m_proxy->setFilterRole(Qt::DisplayRole);
        // Install the proxy after creating the source model; use mapToSource for indexes received from the view.
        ui->view->setModel(m_proxy);
        ui->view->setSortingEnabled(true);
        ui->view->sortByColumn(0, Qt::AscendingOrder);
        connect(ui->view, &viv::VirtualTableView::sortIndicatorRequested, this, [this](int column, Qt::SortOrder order) {
            ui->status->setText(QStringLiteral("排序 column=%1 order=%2").arg(column).arg(int(order)));
        });

        connect(ui->asc, &QPushButton::clicked, this, [this] {
            ui->view->sortByColumn(0, Qt::AscendingOrder);
        });
        connect(ui->desc, &QPushButton::clicked, this, [this] {
            ui->view->sortByColumn(0, Qt::DescendingOrder);
        });
        connect(ui->filter, &QCheckBox::toggled, this, [this](bool enabled) {
            m_proxy->setFilterFixedString(enabled ? QStringLiteral("1") : QString());
            ui->status->setText(QStringLiteral("当前显示 %1 / %2 行")
                .arg(m_proxy->rowCount()).arg(m_model->rowCount()));
        });
        connect(ui->source, &QPushButton::clicked, this, [this] {
            const QModelIndex source = m_proxy->mapToSource(ui->view->currentIndex());
            ui->status->setText(QStringLiteral("源 row=%1 column=%2").arg(source.row()).arg(source.column()));
        });
    }

    QStandardItemModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_sorting
