#include "ui_selection.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QItemSelectionModel>

namespace treetable_selection {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableSelectionPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableSelectionPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->other->setObjectName(QStringLiteral("vivItemView"));
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
        ui->other->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        ui->other->setCellAdapter(new CellAdapter, true);
        ui->other->setUniformItemHeight(40);
        ui->other->setDefaultColumnWidth(150);
        ui->other->setModel(m_model);
        ui->other->setColumnWidth(0, 280);
        ui->other->setExpansionAnimationEnabled(false);
        ui->other->expandRecursively(m_model->index(0, 0));
        ui->other->setExpansionAnimationEnabled(true);
        // Views of the same model can share selection; each view still maintains its own hover state.
        auto *selection = new QItemSelectionModel(m_model, this);
        ui->view->setSelectionModel(selection);
        ui->other->setSelectionModel(selection);
        ui->other->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        ui->other->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);
        // Use Ctrl and Shift with ExtendedSelection; select programmatically through selectionModel.
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        ui->view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);

        for (auto *view : {ui->view, ui->other}) {
            // Header click selection is independent of body click selection behavior.
            // Row-header selection is on by default; column-header selection is opt-in.
            view->setVerticalHeaderSelectionEnabled(true);
            view->setHorizontalHeaderSelectionEnabled(false);
            // Each header axis shows its state background by default.
            // Set both to false to highlight only body items; header clicks still select.
            view->setHeaderVisualStateBackgroundVisible(Qt::Vertical, true);
            view->setHeaderVisualStateBackgroundVisible(Qt::Horizontal, true);
        }

        connect(ui->rowHeaderSelection, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setVerticalHeaderSelectionEnabled(enabled);
            ui->other->setVerticalHeaderSelectionEnabled(enabled);
        });
        connect(ui->columnHeaderSelection, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setHorizontalHeaderSelectionEnabled(enabled);
            ui->other->setHorizontalHeaderSelectionEnabled(enabled);
        });
        connect(ui->rowHeaderBackground, &QCheckBox::toggled, this, [this](bool visible) {
            ui->view->setHeaderVisualStateBackgroundVisible(Qt::Vertical, visible);
            ui->other->setHeaderVisualStateBackgroundVisible(Qt::Vertical, visible);
        });
        connect(ui->columnHeaderBackground, &QCheckBox::toggled, this, [this](bool visible) {
            ui->view->setHeaderVisualStateBackgroundVisible(Qt::Horizontal, visible);
            ui->other->setHeaderVisualStateBackgroundVisible(Qt::Horizontal, visible);
        });

        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            using Mode = viv::VirtualItemView::SelectionMode;
            const Mode modes[] = {Mode::ExtendedSelection, Mode::SingleSelection, Mode::MultiSelection, Mode::NoSelection};
            ui->view->setSelectionMode(modes[value]);
            ui->other->setSelectionMode(modes[value]);
        });
        // Selection behavior also determines both hover and selected background scope.
        connect(ui->behavior, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            using Behavior = viv::VirtualItemView::SelectionBehavior;
            const Behavior behaviors[] = {Behavior::SelectRows, Behavior::SelectItems};
            ui->view->selectionModel()->clearSelection();
            ui->view->setSelectionBehavior(behaviors[value]);
            ui->other->setSelectionBehavior(behaviors[value]);
        });
        connect(ui->select, &QPushButton::clicked, this, [this] {
            // One QItemSelectionRange covers siblings only; use multiple ranges across parents.
            const QModelIndex parent = m_model->index(0, 0);
            ui->view->expand(parent);
            ui->other->expand(parent);
            const QItemSelection range(m_model->index(1, 0, parent), m_model->index(4, m_model->columnCount() - 1, parent));
            ui->view->selectionModel()->select(range, QItemSelectionModel::ClearAndSelect);
            // Current and selected items are separate; NoUpdate preserves the selected range.
            ui->view->selectionModel()->setCurrentIndex(m_model->index(4, 0, parent), QItemSelectionModel::NoUpdate);
        });
        connect(ui->query, &QPushButton::clicked, this, [this] {
            ui->status->setText(QStringLiteral("当前 row=%1，选中单元格=%2").arg(ui->view->currentIndex().row()).arg(ui->view->selectionModel()->selectedIndexes().size()));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_selection
