#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("21 排序与代理模型：通过代理模型排序过滤，视图只发起排序，数据顺序由 model 管理"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_asc = new QPushButton(QStringLiteral("首列升序"), this);
        controls->addWidget(m_asc);
        m_desc = new QPushButton(QStringLiteral("首列降序"), this);
        controls->addWidget(m_desc);
        m_filter = new QCheckBox(QStringLiteral("仅显示条目名称包含 1 的行"), this);
        m_filter->setChecked(false);
        controls->addWidget(m_filter);
        m_source = new QPushButton(QStringLiteral("查询源索引"), this);
        controls->addWidget(m_source);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
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
        m_view->setModel(m_model);
        m_proxy = new QSortFilterProxyModel(m_view);
        m_proxy->setSourceModel(m_model);
        m_proxy->setDynamicSortFilter(true);
        // Filter the displayed row name in the first column only.
        m_proxy->setFilterKeyColumn(0);
        m_proxy->setFilterRole(Qt::DisplayRole);
        // Install the proxy after creating the source model; use mapToSource for indexes received from the view.
        m_view->setModel(m_proxy);
        m_view->setSortingEnabled(true);
        m_view->sortByColumn(0, Qt::AscendingOrder);
        connect(m_view, &viv::VirtualTableView::sortIndicatorRequested, this, [this](int column, Qt::SortOrder order) {
            m_status->setText(QStringLiteral("排序 column=%1 order=%2").arg(column).arg(int(order)));
        });

        connect(m_asc, &QPushButton::clicked, this, [this] {
            m_view->sortByColumn(0, Qt::AscendingOrder);
        });
        connect(m_desc, &QPushButton::clicked, this, [this] {
            m_view->sortByColumn(0, Qt::DescendingOrder);
        });
        connect(m_filter, &QCheckBox::toggled, this, [this](bool enabled) {
            m_proxy->setFilterFixedString(enabled ? QStringLiteral("1") : QString());
            m_status->setText(QStringLiteral("当前显示 %1 / %2 行")
                .arg(m_proxy->rowCount()).arg(m_model->rowCount()));
        });
        connect(m_source, &QPushButton::clicked, this, [this] {
            const QModelIndex source = m_proxy->mapToSource(m_view->currentIndex());
            m_status->setText(QStringLiteral("源 row=%1 column=%2").arg(source.row()).arg(source.column()));
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_asc = nullptr;
    QPushButton *m_desc = nullptr;
    QCheckBox *m_filter = nullptr;
    QPushButton *m_source = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_sorting
