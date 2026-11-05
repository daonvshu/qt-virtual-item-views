#include "ui_binding_context.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableBindingContextPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableBindingContextPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Both adapters receive complete rowData; switching modes changes widget granularity.
        ui->view->setTableAdapter(new RowAdapter, true);
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        ui->view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(8, 3, ui->view);
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
        ui->view->setItemDataRole(viv::kDefaultItemDataRole);
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
        ui->view->setModel(m_model);
        // Expand through the view; model row numbers remain local to each parent.
        ui->view->setExpansionAnimationEnabled(false);
        ui->view->expandRecursively(m_model->index(0, 0));
        ui->view->setExpansionAnimationEnabled(true);
        ui->view->setColumnWidth(0, 280);

        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setMaterializationMode(mode == 0 ? viv::VirtualTreeTableView::MaterializationMode::CellWidgets
                                                    : viv::VirtualTreeTableView::MaterializationMode::RowWidgets);
        });
        connect(ui->update, &QPushButton::clicked, this, [this] {
            const QModelIndex current = ui->view->currentIndex();
            if (!current.isValid()) return;
            const QModelIndex payloadIndex = current.siblingAtColumn(0);
            auto record = payloadIndex.data(viv::kDefaultItemDataRole).value<TreeTableRecord>();
            record.progress = (record.progress + 10) % 101;
            // The setData notification rebinds every materialized cell in this row.
            m_model->setData(payloadIndex, QVariant::fromValue(record), viv::kDefaultItemDataRole);
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_binding_context
