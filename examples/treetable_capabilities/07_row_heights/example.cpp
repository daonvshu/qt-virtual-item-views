#include "ui_row_heights.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>

namespace treetable_row_heights {

class RowWidget : public QWidget
{
public:
    explicit RowWidget(QWidget *parent) : QWidget(parent) { setupUi(); }
    QVector<QLabel *> labels;
    QSize sizeHint() const override
    {
        int height = 40;
        for (QLabel *label : labels) {
            const int width = qMax(40, label->parentWidget()->width() - 16);
            height = qMax(height, label->fontMetrics().boundingRect(
                QRect(0, 0, width, 100000), Qt::TextWordWrap, label->text()).height() + 16);
        }
        return QSize(0, height);
    }
private:
    void setupUi()
    {
        for (int column = 0; column < 8; ++column) {
            // The framework manages ColumnHost position, width, visibility and clipping in frozen panes.
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(8, 0, 8, 0);
            auto *label = new QLabel(host);
            layout->addWidget(label);
            label->setWordWrap(true);
            labels.append(label);
        }
    }
};
class RowAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new RowWidget(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<RowWidget *>(widget);
        for (int column = 0; column < row->labels.size(); ++column)
            row->labels[column]->setText(index.siblingAtColumn(column).data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        for (QLabel *label : static_cast<RowWidget *>(widget)->labels)
            label->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(0, 40); }
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableRowHeightsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableRowHeightsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Use one QWidget per row and transfer adapter ownership to the view.
        ui->view->setTableAdapter(new RowAdapter, true);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        // Resize columns to demonstrate wrapping; row resizing stays disabled for automatic heights.
        ui->view->setColumnResizeEnabled(true);
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
        ui->view->setCurrentIndex(m_model->index(0, 0));
        // Variable allows different row heights; estimatedSize supplies estimates until measurement.
        ui->view->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
        ui->view->setEstimatedItemHeight(40);
        // Column resizing also schedules measurement; sizeHint() reads the current host widths.
        ui->view->setAutoMeasureItemHeight(true);
        // Explicit heights take precedence by default; MeasuredWins gives content measurement priority.
        ui->view->setRowSizePolicy(viv::VirtualTreeTableView::RowSizePolicy::ExplicitWins);
        const auto fillText = [this](const auto &visit, const QModelIndex &parent) -> void {
            for (int row = 0; row < m_model->rowCount(parent); ++row) {
                const QModelIndex node = m_model->index(row, 0, parent);
                m_model->setData(node, node.data().toString() + QStringLiteral(" 动态内容随列宽换行。").repeated(1 + row % 5));
                visit(visit, node);
            }
        };
        fillText(fillText, {});

        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setAutoMeasureItemHeight(mode == 0);
            if (mode == 0)
                ui->view->setItemHeightMode(viv::VirtualItemView::ItemHeightMode::Variable);
            else
                ui->view->setUniformItemHeight(40);
        });
        connect(ui->explicitHeight, &QPushButton::clicked, this, [this] {
            // Geometry row APIs use flattened visible rows; model indexes remain hierarchical.
            const auto row = ui->view->visibilityIndex()->visibleRowForIndex(ui->view->currentIndex());
            if (row >= 0) ui->view->setRowHeight(int(row), 100);
        });
        connect(ui->clear, &QPushButton::clicked, this, [this] {
            const auto row = ui->view->visibilityIndex()->visibleRowForIndex(ui->view->currentIndex());
            if (row >= 0) ui->view->clearRowHeight(int(row));
        });
        connect(ui->text, &QPushButton::clicked, this, [this] {
            const auto index = ui->view->currentIndex().siblingAtColumn(0);
            if (!index.isValid()) return;
            m_model->setData(index, index.data().toString() + QStringLiteral("追加文本 ").repeated(12));
        });
        connect(ui->policy, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setRowSizePolicy(value == 0 ? viv::VirtualTreeTableView::RowSizePolicy::ExplicitWins : viv::VirtualTreeTableView::RowSizePolicy::MeasuredWins);
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_row_heights
