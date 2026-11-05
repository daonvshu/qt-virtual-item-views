#include "ui_headers.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QVariant>
#include <virtualitemviews/virtualheaderview.h>

namespace treetable_headers {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableHeadersPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableHeadersPage> ui;

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
        // The default label header uses QWidgets and supports column dragging and animation.
        ui->view->setHorizontalHeaderVisible(true);
        ui->view->setVerticalHeaderVisible(true);
        ui->view->setHeaderHeight(32);
        ui->view->setVerticalHeaderWidth(50);
        ui->view->setColumnDragEnabled(true);
        ui->view->setColumnResizeEnabled(true);
        ui->view->setVerticalHeaderResizeEnabled(true);
        ui->view->setHeaderAnimationEnabled(true);
        ui->view->setHeaderAnimationDuration(300);
        ui->view->setColumnFollowsHeaderVisual(true);

        connect(ui->horizontal, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setHorizontalHeaderVisible(enabled);
        });
        connect(ui->vertical, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setVerticalHeaderVisible(enabled);
        });
        connect(ui->height, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setHeaderHeight(value);
        });
        connect(ui->width, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setVerticalHeaderWidth(value);
        });
        connect(ui->follow, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setColumnFollowsHeaderVisual(enabled);
        });
        connect(ui->move, &QPushButton::clicked, this, [this] {
            ui->view->moveColumn(2, 0, viv::VirtualTreeTableView::MoveAnimation::Animate);
        });
        const auto updateStyles = [this] { applyHeaderStyles(); };
        connect(ui->styles, &QCheckBox::toggled, this, updateStyles);
        connect(ui->alignment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateStyles);
        connect(ui->padding, QOverload<int>::of(&QSpinBox::valueChanged), this, updateStyles);
        connect(ui->fontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, updateStyles);
        connect(ui->bold, &QCheckBox::toggled, this, updateStyles);
        connect(ui->modelStyle, &QCheckBox::toggled, this, [this](bool enabled) {
            // Model roles override header defaults for this logical section only.
            QFont font = ui->view->font();
            font.setPointSize(12);
            font.setBold(true);
            font.setItalic(true);
            m_model->setHeaderData(1, Qt::Horizontal, enabled ? QVariant(font) : QVariant(), Qt::FontRole);
            m_model->setHeaderData(1, Qt::Horizontal,
                enabled ? QVariant(QColor(QStringLiteral("#b91c1c"))) : QVariant(), Qt::ForegroundRole);
            m_model->setHeaderData(1, Qt::Horizontal,
                enabled ? QVariant(int(Qt::AlignRight | Qt::AlignVCenter)) : QVariant(), Qt::TextAlignmentRole);
        });
        connect(ui->freeze, &QCheckBox::toggled, this, [this](bool enabled) {
            // Clones inherit the corresponding header's appearance, including later changes.
            ui->view->setFrozenColumns(enabled ? QVector<int>{0} : QVector<int>{});
        });
    }

    void applyHeaderStyles()
    {
        const bool enabled = ui->styles->isChecked();
        const Qt::Alignment alignments[] = {
            Qt::AlignLeft | Qt::AlignVCenter, Qt::AlignCenter, Qt::AlignRight | Qt::AlignVCenter
        };
        QFont font = ui->view->font();
        font.setPointSize(ui->fontSize->value());
        font.setBold(ui->bold->isChecked());
        // Access the public header interface directly: no cast, adapter or subclass is needed.
        for (auto *header : {ui->view->horizontalHeader(), ui->view->verticalHeader()}) {
            header->setTextAlignment(enabled ? alignments[ui->alignment->currentIndex()]
                                             : Qt::Alignment(Qt::AlignCenter));
            header->setContentPadding(enabled ? QMargins(ui->padding->value(), 2, ui->padding->value(), 2)
                                               : QMargins());
            header->setFont(enabled ? font : QFont());
        }
        // Each axis owns independent colors; the default corner follows the horizontal header.
        ui->view->horizontalHeader()->setBackgroundColor(enabled ? QColor(QStringLiteral("#eef6ff")) : QColor());
        ui->view->horizontalHeader()->setForegroundColor(enabled ? QColor(QStringLiteral("#334155")) : QColor());
        ui->view->verticalHeader()->setBackgroundColor(enabled ? QColor(QStringLiteral("#f5f0ff")) : QColor());
        ui->view->verticalHeader()->setForegroundColor(enabled ? QColor(QStringLiteral("#7c3aed")) : QColor());
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_headers
