#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyle>
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("10 缩进与分支配置：调整默认图标的大小、颜色、对齐及 padding。单图标随展开旋转，双图标直接切换；隐藏分支区域仍保留层级缩进。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        width = new QSpinBox(this);
        width->setRange(0, 60);
        width->setValue(24);
        width->setPrefix(QStringLiteral("分支格宽 "));
        controls->addWidget(width);
        icons = new QCheckBox(QStringLiteral("显示分支图标"), this);
        icons->setChecked(true);
        controls->addWidget(icons);
        hideBranch = new QCheckBox(QStringLiteral("隐藏 branch 区域"), this);
        hideBranch->setToolTip(QStringLiteral("隐藏分支图标及其占位，保留父子层级缩进。双击或方向键仍可展开节点。"));
        controls->addWidget(hideBranch);
        controls->addStretch();
        layout->addLayout(controls);

        auto *appearance = new QHBoxLayout;
        iconSize = new QSpinBox(this);
        iconSize->setRange(1, 32);
        iconSize->setValue(10);
        iconSize->setPrefix(QStringLiteral("图标大小 "));
        appearance->addWidget(iconSize);
        blueIcon = new QCheckBox(QStringLiteral("蓝色图标"), this);
        appearance->addWidget(blueIcon);
        iconMode = new QComboBox(this);
        iconMode->addItems({QStringLiteral("内置三角（旋转）"),
                            QStringLiteral("仅展开图标（旋转）"),
                            QStringLiteral("仅收起图标（旋转）"),
                            QStringLiteral("展开与收起双图标（切换）")});
        appearance->addWidget(iconMode);
        appearance->addStretch();
        layout->addLayout(appearance);

        auto *alignment = new QHBoxLayout;
        horizontalAlignment = new QComboBox(this);
        horizontalAlignment->addItem(QStringLiteral("左对齐"), int(Qt::AlignLeft));
        horizontalAlignment->addItem(QStringLiteral("水平居中"), int(Qt::AlignHCenter));
        horizontalAlignment->addItem(QStringLiteral("右对齐"), int(Qt::AlignRight));
        horizontalAlignment->setCurrentIndex(1);
        alignment->addWidget(horizontalAlignment);
        verticalAlignment = new QComboBox(this);
        verticalAlignment->addItem(QStringLiteral("顶对齐"), int(Qt::AlignTop));
        verticalAlignment->addItem(QStringLiteral("垂直居中"), int(Qt::AlignVCenter));
        verticalAlignment->addItem(QStringLiteral("底对齐"), int(Qt::AlignBottom));
        verticalAlignment->setCurrentIndex(1);
        alignment->addWidget(verticalAlignment);
        alignment->addWidget(new QLabel(QStringLiteral("Padding"), this));
        const QString prefixes[] = {QStringLiteral("左 "), QStringLiteral("上 "),
                                    QStringLiteral("右 "), QStringLiteral("下 ")};
        for (int side = 0; side < 4; ++side) {
            padding[side] = new QSpinBox(this);
            padding[side]->setRange(0, 20);
            padding[side]->setPrefix(prefixes[side]);
            alignment->addWidget(padding[side]);
        }
        alignment->addStretch();
        layout->addLayout(alignment);
        m_view = new viv::VirtualTreeTableView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // A node is identified by its column-zero index, never by its visible row number.
        m_model = new QStandardItemModel(6, 6, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column) {
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
            for (int row = 0; row < m_model->rowCount(); ++row)
                m_model->setData(m_model->index(row, column),
                    QStringLiteral("分组 %1 / 列 %2").arg(row + 1).arg(column + 1));
        }
        appendChildren(m_model);

        // The view owns the adapter through teardown; the QObject parent owns the model.
        // CellWidgets applies tree indentation only to logical column zero.
        m_view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        m_view->setDefaultColumnWidth(150);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);
        m_view->setColumnWidth(0, 280);

        // One indentation cell is reserved per retained level, independently of icon size.
        m_view->setIndentation(24);
        // These convenience APIs match VirtualTreeView and only affect the default renderer.
        // Sizes and padding use logical pixels; the icon shrinks to fit its padded branch cell.
        m_view->setBranchIconSize(10);
        m_view->setBranchIconColor(QColor());
        m_view->setBranchIndicatorAlignment(Qt::AlignCenter);
        m_view->setBranchIndicatorPadding(0, 0, 0, 0);
        m_view->expandRecursively(m_model->index(0, 0));
        connect(width, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualTreeTableView::setIndentation);
        // Remove each node's icon cell while retaining depth * indentation().
        // The width control still changes the distance between hierarchy levels.
        connect(hideBranch, &QCheckBox::toggled, this, [this](bool hidden) {
            m_view->setBranchAreaVisible(!hidden);
        });
        // This only hides the icon; it preserves the current indentation width.
        connect(icons, &QCheckBox::toggled, m_view, &viv::VirtualTreeTableView::setBranchIndicatorsVisible);
        connect(iconSize, QOverload<int>::of(&QSpinBox::valueChanged),
                m_view, &viv::VirtualTreeTableView::setBranchIconSize);
        connect(blueIcon, &QCheckBox::toggled, this, [this](bool checked) {
            // An invalid color restores native icon colors or palette WindowText for triangles.
            m_view->setBranchIconColor(checked ? QColor(QStringLiteral("#2676c8")) : QColor());
        });
        connect(iconMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            // Supply one icon in its named orientation to share the expansion rotation.
            // Supplying both icons switches immediately; clearing both restores the built-in triangle.
            m_view->setExpandedBranchIcon(mode == 1 || mode == 3
                ? m_view->style()->standardIcon(QStyle::SP_ArrowDown) : QIcon());
            m_view->setCollapsedBranchIcon(mode == 2 || mode == 3
                ? m_view->style()->standardIcon(QStyle::SP_ArrowRight) : QIcon());
        });
        const auto updateAlignment = [this](int) {
            // Alignment is evaluated after removing padding from the indentation cell.
            const Qt::Alignment flags = Qt::Alignment(horizontalAlignment->currentData().toInt())
                | Qt::Alignment(verticalAlignment->currentData().toInt());
            m_view->setBranchIndicatorAlignment(flags);
        };
        connect(horizontalAlignment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateAlignment);
        connect(verticalAlignment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateAlignment);
        for (QSpinBox *side : padding) {
            connect(side, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
                // Excessive padding leaves no drawable icon area without changing node indentation.
                m_view->setBranchIndicatorPadding(padding[0]->value(), padding[1]->value(),
                                                  padding[2]->value(), padding[3]->value());
            });
        }
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QSpinBox *width = nullptr;
    QCheckBox *icons = nullptr;
    QCheckBox *hideBranch = nullptr;
    QSpinBox *iconSize = nullptr;
    QCheckBox *blueIcon = nullptr;
    QComboBox *iconMode = nullptr;
    QComboBox *horizontalAlignment = nullptr;
    QComboBox *verticalAlignment = nullptr;
    QSpinBox *padding[4] = {};
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_indentation
