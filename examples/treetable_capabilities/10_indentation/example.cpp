#include "ui_indentation.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyle>

namespace treetable_indentation {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableIndentationPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableIndentationPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
        ui->horizontalAlignment->setItemData(0, int(Qt::AlignLeft));
        ui->horizontalAlignment->setItemData(1, int(Qt::AlignHCenter));
        ui->horizontalAlignment->setItemData(2, int(Qt::AlignRight));
        ui->verticalAlignment->setItemData(0, int(Qt::AlignTop));
        ui->verticalAlignment->setItemData(1, int(Qt::AlignVCenter));
        ui->verticalAlignment->setItemData(2, int(Qt::AlignBottom));
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

        // One indentation cell is reserved per retained level, independently of icon size.
        ui->view->setIndentation(24);
        // These convenience APIs match VirtualTreeView and only affect the default renderer.
        // Sizes and padding use logical pixels; the icon shrinks to fit its padded branch cell.
        ui->view->setBranchIconSize(10);
        ui->view->setBranchIconColor(QColor());
        ui->view->setBranchIndicatorAlignment(Qt::AlignCenter);
        ui->view->setBranchIndicatorPadding(0, 0, 0, 0);
        ui->view->expandRecursively(m_model->index(0, 0));
        connect(ui->width, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualTreeTableView::setIndentation);
        // Remove each node's icon cell while retaining depth * indentation().
        // The width control still changes the distance between hierarchy levels.
        connect(ui->hideBranch, &QCheckBox::toggled, this, [this](bool hidden) {
            ui->view->setBranchAreaVisible(!hidden);
        });
        // This only hides the icon; it preserves the current indentation width.
        connect(ui->icons, &QCheckBox::toggled, ui->view, &viv::VirtualTreeTableView::setBranchIndicatorsVisible);
        connect(ui->iconSize, QOverload<int>::of(&QSpinBox::valueChanged),
                ui->view, &viv::VirtualTreeTableView::setBranchIconSize);
        connect(ui->blueIcon, &QCheckBox::toggled, this, [this](bool checked) {
            // An invalid color restores native icon colors or palette WindowText for triangles.
            ui->view->setBranchIconColor(checked ? QColor(QStringLiteral("#2676c8")) : QColor());
        });
        connect(ui->iconMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            // Supply one icon in its named orientation to share the expansion rotation.
            // Supplying both icons switches immediately; clearing both restores the built-in triangle.
            ui->view->setExpandedBranchIcon(mode == 1 || mode == 3
                ? ui->view->style()->standardIcon(QStyle::SP_ArrowDown) : QIcon());
            ui->view->setCollapsedBranchIcon(mode == 2 || mode == 3
                ? ui->view->style()->standardIcon(QStyle::SP_ArrowRight) : QIcon());
        });
        const auto updateAlignment = [this](int) {
            // Alignment is evaluated after removing padding from the indentation cell.
            const Qt::Alignment flags = Qt::Alignment(ui->horizontalAlignment->currentData().toInt())
                | Qt::Alignment(ui->verticalAlignment->currentData().toInt());
            ui->view->setBranchIndicatorAlignment(flags);
        };
        connect(ui->horizontalAlignment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateAlignment);
        connect(ui->verticalAlignment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateAlignment);
        for (QSpinBox *side : {ui->padding0, ui->padding1, ui->padding2, ui->padding3}) {
            connect(side, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
                // Excessive padding leaves no drawable icon area without changing node indentation.
                ui->view->setBranchIndicatorPadding(ui->padding0->value(), ui->padding1->value(),
                                                  ui->padding2->value(), ui->padding3->value());
            });
        }
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_indentation
