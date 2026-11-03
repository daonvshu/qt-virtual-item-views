#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace treetable_spans {

class GroupSpans : public viv::TableSpanProvider
{
public:
    viv::TableSpan spanAt(const QModelIndex &index) const override
    {
        if (!index.parent().isValid() || index.parent().parent().isValid()) return {};
        if (index.row() == 1 && index.column() == 1) return {1, 3};
        if (index.row() == 4 && index.column() == 2) return {3, 2};
        return {};
    }
    viv::TableSpan maximumSpan() const override { return {3, 3}; }
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

// Each chapter owns this helper; no implementation is shared with other pages.
// Only column zero owns children, and every child row supplies the full column schema.
static void appendChildren(QStandardItemModel *model)
{
    const int columns = model->columnCount();
    for (int group = 0; group < model->rowCount(); ++group) {
        auto *branch = model->item(group, 0);
        branch->removeRows(0, branch->rowCount());
        branch->setColumnCount(columns);
        for (int row = 0; row < 8; ++row) {
            QList<QStandardItem *> cells;
            for (int column = 0; column < columns; ++column)
                cells.append(new QStandardItem(QStringLiteral("节点 %1.%2 / 列 %3")
                    .arg(group + 1).arg(row + 1).arg(column + 1)));
            branch->appendRow(cells);
            if (row == 0) {
                for (int leaf = 0; leaf < 3; ++leaf) {
                    QList<QStandardItem *> leaves;
                    for (int column = 0; column < columns; ++column)
                        leaves.append(new QStandardItem(QStringLiteral("叶子 %1.%2.%3 / 列 %4")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1).arg(column + 1)));
                    cells.first()->appendRow(leaves);
                }
            }
        }
    }
}

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
        auto *description = new QLabel(QStringLiteral("28 同级单元格合并：跨行合并只覆盖连续可见的同级节点，且不能跨 pane。"), this);
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
        m_view = new viv::VirtualTreeTableView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(8, 8, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        appendChildren(m_model);
        m_view->setModel(m_model);
        // Expand through the view; model row numbers remain local to each parent.
        m_view->setExpansionAnimationEnabled(false);
        m_view->expandRecursively(m_model->index(0, 0));
        m_view->setExpansionAnimationEnabled(true);
        m_view->setColumnWidth(0, 280);
        applySpans();

        connect(m_provider, &QPushButton::clicked, this, [this] {
            // The view owns the provider, which reports spans only at anchors and declares its maximum span.
            m_view->setSpanProvider(new GroupSpans, true);
        });
        connect(m_clear, &QPushButton::clicked, this, [this] {
            m_view->setSpanProvider(nullptr);
        });
        connect(m_apply, &QPushButton::clicked, this, [this] {
            applySpans();
        });
        connect(m_query, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            const QModelIndex anchor = m_view->anchorIndex(index);
            const QRect rect = m_view->spanRect(anchor);
            m_status->setText(QStringLiteral("锚点=(%1,%2)，区域=%3×%4").arg(anchor.row()).arg(anchor.column()).arg(rect.width()).arg(rect.height()));
        });
    }

    void applySpans()
    {
        m_view->setSpanProvider(nullptr);
        const QModelIndex parent = m_model->index(0, 0);
        m_view->expandRecursively(parent);
        const QModelIndex horizontal = m_model->index(1, 1, parent);
        const QModelIndex block = m_model->index(4, 2, parent);
        m_model->setData(horizontal, QStringLiteral("同一子节点横跨三列"));
        m_model->setData(block, QStringLiteral("连续三个同级叶子 × 两列"));
        // setSpan takes visible row numbers; convert from the real node index first.
        const auto first = m_view->visibilityIndex()->visibleRowForIndex(horizontal);
        const auto second = m_view->visibilityIndex()->visibleRowForIndex(block);
        if (first >= 0) m_view->setSpan(int(first), 1, 1, 3);
        if (second >= 0) m_view->setSpan(int(second), 2, 3, 2);
    }

    viv::VirtualTreeTableView *m_view = nullptr;
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

} // namespace treetable_spans
