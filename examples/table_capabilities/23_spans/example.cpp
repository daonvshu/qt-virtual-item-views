#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace table_spans {

class GroupSpans : public viv::TableSpanProvider
{
public:
    viv::TableSpan spanAt(const QModelIndex &index) const override
    {
        return index.column() == 0 && index.row() % 5 == 0 ? viv::TableSpan{1, 3} : viv::TableSpan{};
    }
    viv::TableSpan maximumSpan() const override { return {1, 3}; }
};

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
        auto *description = new QLabel(QStringLiteral("23 合并单元格：同列跨行或同行跨列合并，覆盖格命中折回锚点，合并不跨 pane"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_clear = new QPushButton(QStringLiteral("取消全部合并"), this);
        controls->addWidget(m_clear);
        m_apply = new QPushButton(QStringLiteral("应用合并"), this);
        controls->addWidget(m_apply);
        m_query = new QPushButton(QStringLiteral("查询当前锚点"), this);
        controls->addWidget(m_query);
        m_provider = new QPushButton(QStringLiteral("业务规则 provider"), this);
        controls->addWidget(m_provider);
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
        // setSpan creates a view-owned map provider; custom rules can use setSpanProvider instead.
        m_view->setSpan(1, 1, 1, 3);
        m_view->setSpan(4, 2, 3, 2);
        m_model->setData(m_model->index(1, 1), QStringLiteral("横跨三列"));
        m_model->setData(m_model->index(4, 2), QStringLiteral("三行 × 两列"));

        connect(m_provider, &QPushButton::clicked, this, [this] {
            // The view owns the provider, which reports spans only at anchors and declares its maximum span.
            m_view->setSpanProvider(new GroupSpans, true);
        });
        connect(m_clear, &QPushButton::clicked, this, [this] {
            m_view->setSpanProvider(nullptr);
        });
        connect(m_apply, &QPushButton::clicked, this, [this] {
            m_view->setSpanProvider(nullptr);
            m_view->setSpan(1, 1, 1, 3);
            m_view->setSpan(4, 2, 3, 2);
        });
        connect(m_query, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            const QModelIndex anchor = m_view->anchorIndex(index);
            const QRect rect = m_view->spanRect(anchor);
            m_status->setText(QStringLiteral("锚点=(%1,%2)，区域=%3×%4").arg(anchor.row()).arg(anchor.column()).arg(rect.width()).arg(rect.height()));
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_clear = nullptr;
    QPushButton *m_apply = nullptr;
    QPushButton *m_query = nullptr;
    QPushButton *m_provider = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_spans
