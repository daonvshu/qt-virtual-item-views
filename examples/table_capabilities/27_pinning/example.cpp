#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("27 屏外保留：编辑或异步活动需要控件时显式保留，用完释放，索引不随屏外回收失去身份"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_pin = new QPushButton(QStringLiteral("保留当前格"), this);
        controls->addWidget(m_pin);
        m_query = new QPushButton(QStringLiteral("查询保留控件"), this);
        controls->addWidget(m_query);
        m_release = new QPushButton(QStringLiteral("释放保留"), this);
        controls->addWidget(m_release);
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
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        // Warn above 20 pinned items; this soft limit does not release widgets automatically.
        m_view->setMaxPinnedItems(20);

        connect(m_pin, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            if (!index.isValid()) return;
            if (m_pinned.isValid()) m_view->setItemPinned(m_pinned, false);
            m_pinned = index;
            // CellWidgets pins the individual cell; RowWidgets pins the entire row.
            m_view->setItemPinned(index, true);
            m_status->setText(QStringLiteral("已保留 (%1,%2)，可滚出屏幕后查询").arg(index.row()).arg(index.column()));
        });
        connect(m_query, &QPushButton::clicked, this, [this] {
            m_status->setText(QStringLiteral("index 有效=%1，widget 存在=%2").arg(m_pinned.isValid()).arg(m_view->cellWidget(m_pinned) != nullptr));
        });
        connect(m_release, &QPushButton::clicked, this, [this] {
            if (m_pinned.isValid()) m_view->setItemPinned(m_pinned, false);
            m_pinned = QPersistentModelIndex();
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_pin = nullptr;
    QPushButton *m_query = nullptr;
    QPushButton *m_release = nullptr;
    QPersistentModelIndex m_pinned;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_pinning
