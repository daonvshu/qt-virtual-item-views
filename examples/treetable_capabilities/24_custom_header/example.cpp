#include "example.h"
#include "styled_headers.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <virtualitemviews/styledheadersection.h>
#include <QPointer>

namespace treetable_custom_header {

// Keep the framework's panel, state backgrounds and separators around child widgets.
class HeaderSection : public viv::StyledHeaderSection
{
public:
    explicit HeaderSection(QWidget *parent) : viv::StyledHeaderSection(parent) { setupUi(); }
    QLabel *title = nullptr;
    QPushButton *sort = nullptr;
private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 0, 6, 0);
        title = new QLabel(this);
        sort = new QPushButton(QStringLiteral("排序"), this);
        sort->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(title, 1);
        layout->addWidget(sort);
    }
};
class HeaderAdapter : public viv::HeaderSectionAdapter
{
public:
    explicit HeaderAdapter(viv::VirtualTreeTableView &view) : m_view(view) {}
    void setLabelModel(QAbstractItemModel *model) override { m_model = model; }
    QWidget *createSection(viv::WidgetType, QWidget *parent) override
    {
        auto *section = new HeaderSection(parent);
        QObject::connect(section->sort, &QPushButton::clicked, section, [this, section] {
            // Header widgets are also reused; read the logical column saved by the latest bind.
            if (section->logicalIndex() >= 0)
                m_view.sortByColumn(section->logicalIndex(), Qt::AscendingOrder);
        });
        return section;
    }
    void bindSection(QWidget *widget, int logicalIndex) override
    {
        // Bind the identity used by built-in state painting and the button callback.
        viv::HeaderSectionAdapter::bindSection(widget, logicalIndex);
        auto *section = static_cast<HeaderSection *>(widget);
        section->title->setText(m_model ? m_model->headerData(logicalIndex, Qt::Horizontal).toString() : QString());
    }
    void unbindSection(QWidget *widget, int logicalIndex) override
    {
        // Clear the framework identity before this section is reused.
        viv::HeaderSectionAdapter::unbindSection(widget, logicalIndex);
        auto *section = static_cast<HeaderSection *>(widget);
        section->title->clear();
    }
private:
    viv::VirtualTreeTableView &m_view;
    QPointer<QAbstractItemModel> m_model;
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
        auto *description = new QLabel(QStringLiteral("24 表头样式、Adapter 与 Corner：先展开分组，再操作本章控件观察树表行为。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        m_customCorner = new QCheckBox(QStringLiteral("左上角使用全选按钮"), this);
        layout->addWidget(m_customCorner);
        m_view = new viv::VirtualTreeTableView(this);
        layout->addWidget(m_view, 1);
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
        // Replace the default header's section adapter directly; true transfers ownership.
        // The header supplies the current model and geometry and rebuilds frozen pane clones.
        m_view->horizontalHeader()->setAdapter(new HeaderAdapter(*m_view), true);
        m_view->setHeaderHeight(48);
        m_view->setColumnDragEnabled(true);
        m_view->setSortingEnabled(true);
        m_view->setFrozenColumns({0});

        // The default corner shares the built-in header style, without a separate adapter.
        connect(m_customCorner, &QCheckBox::toggled, this, [this](bool enabled) {
            if (!enabled) {
                // nullptr restores the default; the view releases the old custom widget.
                m_view->setHeaderCornerWidget(nullptr);
                return;
            }
            auto *button = new QPushButton(QStringLiteral("全选"));
            button->setFocusPolicy(Qt::NoFocus);
            connect(button, &QPushButton::clicked, m_view, [this] {
                // Each parent needs its own sibling range; root ranges do not include descendants.
                QItemSelection all;
                const auto collect = [this, &all](const auto &visit, const QModelIndex &parent) -> void {
                    const int rows = m_model->rowCount(parent);
                    if (rows == 0) return;
                    all.select(m_model->index(0, 0, parent),
                               m_model->index(rows - 1, m_model->columnCount(parent) - 1, parent));
                    for (int row = 0; row < rows; ++row) visit(visit, m_model->index(row, 0, parent));
                };
                collect(collect, {});
                m_view->selectionModel()->select(all, QItemSelectionModel::ClearAndSelect);
            });
            // The view owns the widget and tracks both header sizes and visibility.
            m_view->setHeaderCornerWidget(button);
        });
    }

    viv::VirtualTreeTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QCheckBox *m_customCorner = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    auto *tabs = new QTabWidget(parent);
    tabs->addTab(treetable_header_states::createExample(tabs), QStringLiteral("表头样式与绘制"));
    tabs->addTab(new Example(tabs), QStringLiteral("组合控件与 corner"));
    return tabs;
}

} // namespace treetable_custom_header
