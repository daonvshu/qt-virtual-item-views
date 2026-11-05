#include "ui_expansion.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>

namespace treetable_expansion {

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
        // Every bind replaces content from the previous node and logical column.
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableExpansionPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableExpansionPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // A node is identified by its column-zero index, never by its visible row number.
        m_model = new QStandardItemModel(6, 6, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column) {
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
            for (int row = 0; row < m_model->rowCount(); ++row)
                m_model->setData(m_model->index(row, column),
                    QStringLiteral("分组 %1 / 列 %2").arg(row + 1).arg(column + 1));
        }
        appendChildren(m_model);

        // The view owns the adapter through teardown; the QObject parent owns the model.
        // CellWidgets applies tree indentation only to logical column zero.
        ui->view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        ui->view->setDefaultColumnWidth(150);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);
        ui->view->setColumnWidth(0, 280);

        // Expansion animation is enabled by default; it changes visuals, not model identities.
        ui->view->setExpansionAnimationDuration(300);
        // Fading is enabled by default. The checkbox calls setExpansionFadeEnabled(false)
        // to disable it without disabling the live height transition.
        ui->fade->setChecked(ui->view->expansionFadeEnabled());
        ui->view->setCurrentIndex(m_model->index(0, 0));
        connect(ui->open, &QPushButton::clicked, this, [this] { ui->view->expand(ui->view->currentIndex()); });
        connect(ui->close, &QPushButton::clicked, this, [this] { ui->view->collapse(ui->view->currentIndex()); });
        connect(ui->toggle, &QPushButton::clicked, this, [this] { ui->view->toggleExpanded(ui->view->currentIndex()); });
        connect(ui->recursive, &QPushButton::clicked, this, [this] { ui->view->expandRecursively(ui->view->currentIndex()); });
        connect(ui->animated, &QCheckBox::toggled, ui->view, &viv::VirtualTreeTableView::setExpansionAnimationEnabled);
        connect(ui->fade, &QCheckBox::toggled, ui->view, &viv::VirtualTreeTableView::setExpansionFadeEnabled);
        connect(ui->animated, &QCheckBox::toggled, ui->fade, &QWidget::setEnabled);
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged), ui->view,
                &viv::VirtualTreeTableView::setExpansionAnimationDuration);
        const auto report = [this](const QModelIndex &index) {
            ui->status->setText(QStringLiteral("%1 / expanded=%2 / hasChildren=%3 / 可见树节点=%4")
                .arg(index.data().toString()).arg(ui->view->isExpanded(index))
                .arg(ui->view->hasChildren(index)).arg(ui->view->visibleRowCount()));
        };
        connect(ui->view, &viv::VirtualTreeTableView::expanded, this, report);
        connect(ui->view, &viv::VirtualTreeTableView::collapsed, this, report);
        connect(ui->all, &QPushButton::clicked, this, [this, report] {
            ui->view->collapseAll();
            report(ui->view->currentIndex());
        });
        // Populate the status before showing the page. An initially empty label
        // grows on the first expanded signal, resizing the tree and ending its animation.
        report(ui->view->currentIndex());
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_expansion
