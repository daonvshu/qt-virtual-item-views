#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

struct TreeTableRecord
{
    QString title;
    QString owner;
    int progress = 0;
};
Q_DECLARE_METATYPE(TreeTableRecord)
namespace treetable_binding_context {

class RowWidget : public QWidget
{
public:
    explicit RowWidget(QWidget *parent) : QWidget(parent) { setupUi(); }
    QVector<QLabel *> labels;
private:
    void setupUi()
    {
        for (int column = 0; column < 3; ++column) {
            // The framework manages ColumnHost position, width, visibility and clipping in frozen panes.
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(8, 0, 8, 0);
            auto *label = new QLabel(host);
            layout->addWidget(label);
            labels.append(label);
        }
    }
};
class RowAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new RowWidget(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const viv::ItemBindingContext &context) override
    {
        // context.depth is relative to rootIndex(); index preserves the complete parent chain.
        const TreeTableRecord record = context.rowData.value<TreeTableRecord>();
        const QStringList fields{record.title, record.owner, QString::number(record.progress)};
        widget->setToolTip(QStringLiteral("depth=%1").arg(context.depth));
        auto *row = static_cast<RowWidget *>(widget);
        for (int column = 0; column < row->labels.size(); ++column)
            row->labels[column]->setText(fields.value(column));
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        for (QLabel *label : static_cast<RowWidget *>(widget)->labels)
            label->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(0, 40); }
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
    void bindCellWidget(QWidget *widget, const viv::ItemBindingContext &context) override
    {
        // Each cell receives the complete row payload; logicalColumn is always a model column.
        // context.depth is relative to rootIndex(); index preserves the complete parent chain.
        const TreeTableRecord record = context.rowData.value<TreeTableRecord>();
        const QStringList fields{record.title, record.owner, QString::number(record.progress)};
        static_cast<QLabel *>(widget)->setText(fields.value(context.logicalColumn));
        widget->setToolTip(QStringLiteral("depth=%1 / logicalColumn=%2").arg(context.depth).arg(context.logicalColumn));
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
        auto *description = new QLabel(QStringLiteral("05 节点结构体与绑定上下文：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_mode = new QComboBox(this);
        m_mode->addItems({QStringLiteral("单元格物化"), QStringLiteral("整行物化")});
        controls->addWidget(m_mode);
        m_update = new QPushButton(QStringLiteral("更新当前行结构体"), this);
        controls->addWidget(m_update);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeTableView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // Both adapters receive complete rowData; switching modes changes widget granularity.
        m_view->setTableAdapter(new RowAdapter, true);
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(8, 3, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        appendChildren(m_model);
        // The payload role is read from column zero of this node, never from its parent.
        m_view->setItemDataRole(viv::kDefaultItemDataRole);
        const auto assignPayloads = [this](const auto &visit, const QModelIndex &parent, int depth) -> void {
            for (int row = 0; row < m_model->rowCount(parent); ++row) {
                const QModelIndex node = m_model->index(row, 0, parent);
                const TreeTableRecord record{node.data().toString(),
                    QStringLiteral("层级 %1 负责人").arg(depth), (row * 13) % 101};
                m_model->setData(node, QVariant::fromValue(record), viv::kDefaultItemDataRole);
                visit(visit, node, depth + 1);
            }
        };
        assignPayloads(assignPayloads, {}, 0);
        m_view->setModel(m_model);
        // Expand through the view; model row numbers remain local to each parent.
        m_view->setExpansionAnimationEnabled(false);
        m_view->expandRecursively(m_model->index(0, 0));
        m_view->setExpansionAnimationEnabled(true);
        m_view->setColumnWidth(0, 280);

        connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            m_view->setMaterializationMode(mode == 0 ? viv::VirtualTreeTableView::MaterializationMode::CellWidgets
                                                    : viv::VirtualTreeTableView::MaterializationMode::RowWidgets);
        });
        connect(m_update, &QPushButton::clicked, this, [this] {
            const QModelIndex current = m_view->currentIndex();
            if (!current.isValid()) return;
            const QModelIndex payloadIndex = current.siblingAtColumn(0);
            auto record = payloadIndex.data(viv::kDefaultItemDataRole).value<TreeTableRecord>();
            record.progress = (record.progress + 10) % 101;
            // The setData notification rebinds every materialized cell in this row.
            m_model->setData(payloadIndex, QVariant::fromValue(record), viv::kDefaultItemDataRole);
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QPushButton *m_update = nullptr;
    QComboBox *m_mode = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_binding_context
