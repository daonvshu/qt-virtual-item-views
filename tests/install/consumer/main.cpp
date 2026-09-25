// 消费端自检：安装出来的 VirtualItemViews 包能不能真的被
//   find_package(VirtualItemViews) + 编译 + 链接 + 运行
// 走完一遍。只使用公开头文件与公开 API，覆盖 list / table / tree / 表头几何与状态 /
// span / 冻结列 / 单元格模式 / accessibility 的入口；任何一项不符合预期就返回非 0。
//
// 跑法见 README 的「安装与消费」：先 cmake --install，再用本工程消费安装前缀。

#include <virtualitemviews/accessibility.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>

#include <QAccessible>
#include <QAbstractItemModel>
#include <QApplication>
#include <QLabel>

#include <cstdio>

namespace {

/// Flat model: \a rows x \a columns, used by the list and the table.
class FlatModel : public QAbstractItemModel
{
public:
    FlatModel(int rows, int columns, QObject *parent = nullptr)
        : QAbstractItemModel(parent)
        , m_rows(rows)
        , m_columns(columns)
    {
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() || row < 0 || row >= m_rows || column < 0 || column >= m_columns)
            return QModelIndex();
        return createIndex(row, column);
    }

    QModelIndex parent(const QModelIndex &) const override { return QModelIndex(); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_columns;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("r%1c%2").arg(index.row()).arg(index.column());
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        return orientation == Qt::Horizontal ? QStringLiteral("列 %1").arg(section)
                                             : QStringLiteral("行 %1").arg(section);
    }

private:
    int m_rows = 0;
    int m_columns = 0;
};

/// Tree model: two roots, each with three children (one level).
class TreeModel : public QAbstractItemModel
{
public:
    explicit TreeModel(QObject *parent = nullptr) : QAbstractItemModel(parent) {}

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (column != 0 || row < 0)
            return QModelIndex();
        if (!parent.isValid())
            return row < kRoots ? createIndex(row, 0, quintptr(kRootMarker)) : QModelIndex();
        if (parent.internalId() == kRootMarker && row < kChildren)
            return createIndex(row, 0, quintptr(parent.row() + 1));
        return QModelIndex();
    }

    QModelIndex parent(const QModelIndex &index) const override
    {
        if (!index.isValid() || index.internalId() == kRootMarker)
            return QModelIndex();
        return createIndex(int(index.internalId()) - 1, 0, quintptr(kRootMarker));
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (!parent.isValid())
            return kRoots;
        return parent.internalId() == kRootMarker ? kChildren : 0;
    }

    int columnCount(const QModelIndex &) const override { return 1; }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        if (index.internalId() == kRootMarker)
            return QStringLiteral("根 %1").arg(index.row());
        return QStringLiteral("节点 %1/%2").arg(index.parent().row()).arg(index.row());
    }

private:
    static constexpr quintptr kRootMarker = 0;
    static constexpr int kRoots = 2;
    static constexpr int kChildren = 3;
};

/// Row widget of the list and of the tree: one label, the smallest thing a
/// business adapter can build.
class LabelAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (auto *label = qobject_cast<QLabel *>(widget))
            label->setText(index.data(Qt::DisplayRole).toString());
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(320, 24); }
};

/// Cell widget of the table's Cell Widget Mode: one label per visible cell.
class LabelCellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (auto *label = qobject_cast<QLabel *>(widget))
            label->setText(index.data(Qt::DisplayRole).toString());
    }
};

int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok)
        ++failures;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    std::printf("viv_consumer: Qt %s, linking against the installed package\n", qVersion());

    FlatModel flat(10000, 5);
    TreeModel treeModel;
    LabelAdapter adapter;
    LabelCellAdapter cellAdapter;

    // -- list ---------------------------------------------------------------
    viv::VirtualListView list;
    list.setAdapter(&adapter);
    list.setModel(&flat);
    list.setUniformItemHeight(24);
    list.resize(400, 300);
    list.show();
    QApplication::processEvents();

    const viv::VirtualViewStats listStats = list.stats();
    check(listStats.logicalItems == 10000, "list sees 10000 logical rows");
    check(list.materializedItemCount() > 0, "list materialized the visible rows");
    check(list.materializedItemCount() < 200, "list materializes only a window");
    check(list.visibleItemRange().isValid(), "list reports a valid visible range");
    check(list.widgetForIndex(flat.index(0, 0)) != nullptr, "row 0 owns a widget");

    list.setVerticalOffset(list.maximumVerticalOffset());
    QApplication::processEvents();
    check(list.verticalOffset() == list.maximumVerticalOffset(), "list scrolled to the end");
    check(list.widgetForIndex(flat.index(9999, 0)) != nullptr, "the last row is materialized");

    // -- table (Cell Widget Mode) -------------------------------------------
    viv::VirtualTableView table;
    table.setModel(&flat);
    table.setCellAdapter(&cellAdapter);
    table.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    table.resize(500, 300);
    table.show();
    QApplication::processEvents();

    check(table.columnCount() == 5, "table sees 5 columns");
    check(table.horizontalHeaderGeometry()->sectionCount() == 5, "header geometry has 5 sections");
    check(table.columnGeometry(1).isValid() && table.columnWidth(1) > 0, "column 1 has geometry");
    check(table.columnAtViewportX(table.columnGeometry(1).viewportX + 1) == 1,
          "column hit test resolves the viewport x");
    check(table.materializedCellCount() > 0, "cells are materialized");
    check(table.cellWidget(flat.index(0, 0)) != nullptr, "cell 0/0 owns a widget");

    table.setColumnHidden(2, true);
    check(table.isColumnHidden(2) && table.columnWidth(2) == 0, "a hidden column has no width");
    const QByteArray headerState = table.saveHeaderState();
    table.setColumnHidden(2, false);
    check(table.restoreHeaderState(headerState) && table.isColumnHidden(2),
          "header state round trips through save/restore");
    table.setColumnHidden(2, false);

    table.setSpan(0, 0, 1, 2);
    check(table.spanAt(flat.index(0, 0)).columnSpan == 2, "the span is anchored on cell 0/0");
    check(table.spanRect(flat.index(0, 0)).width() >= table.columnWidth(0) + table.columnWidth(1),
          "the merged rectangle covers both columns");

    table.setFrozenColumns(QVector<int>{0});
    check(table.isColumnFrozen(0) && table.paneTypeForColumn(0) == viv::TablePane::Type::FrozenLeft,
          "column 0 became a frozen left pane");
    check(table.paneTypeForColumn(4) == viv::TablePane::Type::Scrollable,
          "the remaining columns stay in the scrolling pane");
    int frozenPaneWidth = -1;
    for (const viv::TablePane &pane : table.panes()) {
        if (pane.type == viv::TablePane::Type::FrozenLeft)
            frozenPaneWidth = pane.viewportRect.width();
    }
    check(frozenPaneWidth == table.columnWidth(0), "the frozen pane is exactly one column wide");
    check(table.frozenColumns().contains(0), "the frozen column is queryable");

    // 显式 pane 列表（§43 advanced panes）：任意数量的 pane 与滚动组，且
    // paneSpecs() 与 panes() 索引对齐（没有列的 pane 也会被保留）。
    viv::TablePaneSpec frozenSpec;
    frozenSpec.logicalColumns = QVector<int>{0};
    frozenSpec.scroll = viv::PaneScroll::Frozen;
    viv::TablePaneSpec firstGroupSpec;
    firstGroupSpec.logicalColumns = QVector<int>{1, 2};
    firstGroupSpec.scrollGroup = 0;
    viv::TablePaneSpec secondGroupSpec;
    secondGroupSpec.logicalColumns = QVector<int>{3, 4};
    secondGroupSpec.scrollGroup = 1;
    table.setPanes(QVector<viv::TablePaneSpec>{frozenSpec, firstGroupSpec, secondGroupSpec});
    check(table.paneSpecs().size() == 3, "the explicit pane list keeps all three panes");
    check(table.scrollGroups() == QVector<int>({0, 1}), "both scroll groups are reported");
    check(table.primaryScrollGroup() == 0, "the first scrolling pane owns the primary group");

    // -- tree ---------------------------------------------------------------
    viv::VirtualTreeView tree;
    tree.setAdapter(&adapter);
    tree.setModel(&treeModel);
    tree.setUniformItemHeight(24);
    tree.resize(400, 300);
    tree.show();
    QApplication::processEvents();

    check(tree.visibleRowCount() == 2, "a collapsed tree shows its two roots");
    const QModelIndex root = treeModel.index(0, 0);
    tree.expand(root);
    QApplication::processEvents();
    check(tree.isExpanded(root) && tree.visibleRowCount() == 5,
          "expanding a root reveals its three children");
    tree.collapse(root);
    QApplication::processEvents();
    check(tree.visibleRowCount() == 2, "collapsing restores two visible rows");

#if QT_CONFIG(accessibility)
    viv::installAccessibilityFactory();
    check(QAccessible::queryAccessibleInterface(&list) != nullptr,
          "the accessibility factory exposes the view");
    viv::removeAccessibilityFactory();
#endif

    std::printf("viv_consumer: %s (failures=%d)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
