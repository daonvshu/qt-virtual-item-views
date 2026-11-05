#include "ui_spans.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableSpansPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableSpansPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        ui->view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(8, 8, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        appendChildren(m_model);
        ui->view->setModel(m_model);
        // Expand through the view; model row numbers remain local to each parent.
        ui->view->setExpansionAnimationEnabled(false);
        ui->view->expandRecursively(m_model->index(0, 0));
        ui->view->setExpansionAnimationEnabled(true);
        ui->view->setColumnWidth(0, 280);
        applySpans();

        connect(ui->provider, &QPushButton::clicked, this, [this] {
            // The view owns the provider, which reports spans only at anchors and declares its maximum span.
            ui->view->setSpanProvider(new GroupSpans, true);
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] {
            ui->view->setSpanProvider(nullptr);
        });
        connect(ui->apply, &QPushButton::clicked, this, [this] {
            applySpans();
        });
        connect(ui->query, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            const QModelIndex anchor = ui->view->anchorIndex(index);
            const QRect rect = ui->view->spanRect(anchor);
            ui->status->setText(QStringLiteral("锚点=(%1,%2)，区域=%3×%4").arg(anchor.row()).arg(anchor.column()).arg(rect.width()).arg(rect.height()));
        });
    }

    void applySpans()
    {
        ui->view->setSpanProvider(nullptr);
        const QModelIndex parent = m_model->index(0, 0);
        ui->view->expandRecursively(parent);
        const QModelIndex horizontal = m_model->index(1, 1, parent);
        const QModelIndex block = m_model->index(4, 2, parent);
        m_model->setData(horizontal, QStringLiteral("同一子节点横跨三列"));
        m_model->setData(block, QStringLiteral("连续三个同级叶子 × 两列"));
        // setSpan takes visible row numbers; convert from the real node index first.
        const auto first = ui->view->visibilityIndex()->visibleRowForIndex(horizontal);
        const auto second = ui->view->visibilityIndex()->visibleRowForIndex(block);
        if (first >= 0) ui->view->setSpan(int(first), 1, 1, 3);
        if (second >= 0) ui->view->setSpan(int(second), 2, 3, 2);
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_spans
