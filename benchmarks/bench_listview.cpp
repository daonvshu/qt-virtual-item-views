// Measurements backing the performance claims of the design document:
//  - 1M logical rows must not create a widget per row,
//  - steady state scrolling must not allocate or destroy widgets,
//  - the live widget count must stay proportional to the viewport.
//
// Run it manually, for example:
//   bench_listview --rows 1000000 --steps 2000
//   bench_listview --rows 100000 --steps 500 --compare
//   bench_listview --table --table-columns 100
//   bench_listview --tree

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetrecycler.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QPersistentModelIndex>
#include <QScrollBar>

#include <cstdio>
#include <random>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#endif

namespace {

/// Very light row widget: one label, no business logic.
class BenchRowWidget : public QWidget
{
public:
    explicit BenchRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_label = new QLabel(this);
        m_label->setGeometry(4, 2, 260, 20);
    }

    void setText(const QString &text) { m_label->setText(text); }

private:
    QLabel *m_label = nullptr;
};

class BenchAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        Q_UNUSED(type);
        ++created;
        return new BenchRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<BenchRowWidget *>(widget)->setText(index.data(Qt::DisplayRole).toString());
    }

    QSize estimatedSize(const QModelIndex &index) const override
    {
        Q_UNUSED(index);
        return QSize(400, 24);
    }

    int created = 0;
};

/// Huge model without per-row storage.
class BenchModel : public QAbstractListModel
{
public:
    explicit BenchModel(int count, QObject *parent = nullptr)
        : QAbstractListModel(parent)
        , m_count(qMax(0, count))
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_count;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("Row %1").arg(index.row());
    }

    /// Emits dataChanged for one row (text is generated on the fly).
    void setRowText(int row)
    {
        if (row < 0 || row >= m_count)
            return;
        const QModelIndex index = this->index(row, 0);
        emit dataChanged(index, index, {Qt::DisplayRole});
    }

    void insertRows(int first, int count)
    {
        if (count <= 0)
            return;
        first = qBound(0, first, m_count);
        beginInsertRows(QModelIndex(), first, first + count - 1);
        m_count += count;
        endInsertRows();
    }

    bool removeRows(int first, int count)
    {
        if (count <= 0 || first < 0 || first + count > m_count)
            return false;
        beginRemoveRows(QModelIndex(), first, first + count - 1);
        m_count -= count;
        endRemoveRows();
        return true;
    }

private:
    int m_count = 0;
};

void report(const char *label, double milliseconds)
{
    std::printf("  %-42s %10.2f ms\n", label, milliseconds);
}

void reportCount(const char *label, long long value)
{
    std::printf("  %-42s %10lld\n", label, value);
}

/// Process working set; process-level and therefore only comparable as a delta
/// between scenarios (the architecture document lists memory as a metric).
quint64 processWorkingSetBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return quint64(counters.WorkingSetSize);
#endif
    return 0;
}

void reportMemory(const char *label, quint64 baselineBytes)
{
    const quint64 bytes = processWorkingSetBytes();
    if (bytes == 0) {
        std::printf("  %-42s %10s\n", label, "n/a");
        return;
    }
    if (baselineBytes == 0 || bytes < baselineBytes) {
        std::printf("  %-42s %8.1f MB\n", label, double(bytes) / (1024.0 * 1024.0));
        return;
    }
    std::printf("  %-42s %8.1f MB  (+%.1f)\n", label, double(bytes) / (1024.0 * 1024.0),
                double(bytes - baselineBytes) / (1024.0 * 1024.0));
}

// ---------------------------------------------------------------------------
// Table benchmark: row widget mode vs cell widget mode on a wide table
// (architecture document §39: 100-column horizontal virtualization, column
// resize, section move).
// ---------------------------------------------------------------------------

/// Huge table model without per-cell storage.
class TableModel : public QAbstractTableModel
{
public:
    TableModel(int rows, int columns, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_rows(rows)
        , m_columns(columns)
    {
    }

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
        return orientation == Qt::Horizontal ? QStringLiteral("Column %1").arg(section)
                                             : QString::number(section + 1);
    }

private:
    int m_rows = 0;
    int m_columns = 0;
};

class BenchTableRowWidget : public QWidget
{
public:
    explicit BenchTableRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_label = new QLabel(this);
        m_label->setGeometry(4, 2, 200, 20);
    }

    void setText(const QString &text) { m_label->setText(text); }

private:
    QLabel *m_label = nullptr;
};

class BenchTableAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new BenchTableRowWidget(parent);
    }

    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<BenchTableRowWidget *>(widget)->setText(index.data().toString());
    }

    QSize estimatedSize(const QModelIndex &) const override { return QSize(200, 24); }

    int created = 0;
};

class BenchTableCellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new BenchTableRowWidget(parent);
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<BenchTableRowWidget *>(widget)->setText(index.data().toString());
    }

    int created = 0;
};

// ---------------------------------------------------------------------------
// Tree benchmark (architecture document §35/§39): a huge complete tree that
// stores nothing per node, plus a heap tree for the mutation scenario. The
// point is that a tree is the list kernel on top of a visible-row mapping:
//  - a collapsed sub-tree costs one rowCount() query, never a tree walk,
//  - expanding above the viewport keeps the anchored row in place,
//  - scrolling the visible rows allocates nothing.
// ---------------------------------------------------------------------------

/// Complete level-ordered tree: a node is identified by its position in the
/// per-level numbering, so ten billion nodes cost no memory at all and only the
/// expanded nodes are ever visited.
class PathEncodedTreeModel : public QAbstractItemModel
{
public:
    PathEncodedTreeModel(int rootCount, int branching, int depth, QObject *parent = nullptr)
        : QAbstractItemModel(parent)
        , m_rootCount(qMax(1, rootCount))
        , m_branching(qMax(1, branching))
        , m_depth(qMax(0, depth))
    {
        // m_offsets[level] == number of nodes in all levels above `level`.
        m_offsets.append(0);
        quint64 levelSize = quint64(m_rootCount);
        for (int level = 1; level <= m_depth + 1; ++level) {
            m_offsets.append(m_offsets.last() + levelSize);
            levelSize *= quint64(m_branching);
        }
    }

    /// Nodes of the whole tree in level order; the benchmark's logical size.
    quint64 nodeCount() const { return m_offsets.last(); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (!parent.isValid())
            return m_rootCount;
        if (parent.column() != 0)
            return 0;
        return levelOf(idOf(parent)) < m_depth ? m_branching : 0;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return 1;
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (column != 0)
            return QModelIndex();
        int level = 0;
        quint64 base = 0;
        if (parent.isValid()) {
            if (parent.column() != 0)
                return QModelIndex();
            const quint64 parentId = idOf(parent);
            level = levelOf(parentId) + 1;
            if (level > m_depth)
                return QModelIndex();
            base = parentId - m_offsets.at(level - 1);
        }
        const int count = parent.isValid() ? m_branching : m_rootCount;
        if (row < 0 || row >= count)
            return QModelIndex();
        const quint64 id = m_offsets.at(level) + base * quint64(m_branching) + quint64(row);
        return createIndex(row, column, reinterpret_cast<void *>(quintptr(id + 1)));
    }

    QModelIndex parent(const QModelIndex &index) const override
    {
        if (!index.isValid() || index.column() != 0)
            return QModelIndex();
        const int level = levelOf(idOf(index));
        if (level == 0)
            return QModelIndex();
        const quint64 row = idOf(index) - m_offsets.at(level);
        const quint64 parentRow = row / quint64(m_branching);
        const quint64 parentId = m_offsets.at(level - 1) + parentRow;
        const int reportedRow = level == 1 ? int(parentRow) : int(parentRow % quint64(m_branching));
        return createIndex(reportedRow, 0, reinterpret_cast<void *>(quintptr(parentId + 1)));
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return QStringLiteral("node %1 (L%2)").arg(idOf(index)).arg(levelOf(idOf(index)));
    }

    QVariant headerData(int, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
            return QVariant();
        return QStringLiteral("名称");
    }

private:
    quint64 idOf(const QModelIndex &index) const
    {
        return quint64(reinterpret_cast<quintptr>(index.internalPointer())) - 1;
    }

    int levelOf(quint64 id) const
    {
        int level = 0;
        while (level + 1 < m_offsets.size() && id >= m_offsets.at(level + 1))
            ++level;
        return level;
    }

    int m_rootCount = 1;
    int m_branching = 1;
    int m_depth = 0;
    QVector<quint64> m_offsets;
};

struct BenchTreeNode
{
    QString name;
    BenchTreeNode *parent = nullptr;
    QVector<BenchTreeNode *> children;

    ~BenchTreeNode() { qDeleteAll(children); }
};

/// Ordinary heap tree used for the mutation scenario (insert/remove rows).
class BenchTreeModel : public QAbstractItemModel
{
public:
    BenchTreeModel(int roots, int childrenPerRoot, QObject *parent = nullptr)
        : QAbstractItemModel(parent)
    {
        for (int root = 0; root < roots; ++root) {
            auto *node = new BenchTreeNode;
            node->name = QStringLiteral("root %1").arg(root);
            for (int child = 0; child < childrenPerRoot; ++child) {
                auto *childNode = new BenchTreeNode;
                childNode->name = QStringLiteral("child %1.%2").arg(root).arg(child);
                childNode->parent = node;
                node->children.append(childNode);
            }
            m_roots.append(node);
        }
    }

    ~BenchTreeModel() override { qDeleteAll(m_roots); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        // The invalid root index has column() == -1, so it must be handled
        // before the column check.
        if (parent.isValid() && parent.column() != 0)
            return 0;
        const BenchTreeNode *node = nodeFor(parent);
        return node ? node->children.size() : m_roots.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return 1;
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (column != 0 || (parent.isValid() && parent.column() != 0))
            return QModelIndex();
        BenchTreeNode *node = nodeFor(parent);
        const QVector<BenchTreeNode *> &children = node ? node->children : m_roots;
        if (row < 0 || row >= children.size())
            return QModelIndex();
        return createIndex(row, column, children.at(row));
    }

    QModelIndex parent(const QModelIndex &index) const override
    {
        BenchTreeNode *node = nodeFor(index);
        if (!node || !node->parent)
            return QModelIndex();
        BenchTreeNode *parentNode = node->parent;
        const QVector<BenchTreeNode *> &siblings =
            parentNode->parent ? parentNode->parent->children : m_roots;
        for (int row = 0; row < siblings.size(); ++row) {
            if (siblings.at(row) == parentNode)
                return createIndex(row, 0, parentNode);
        }
        return QModelIndex();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return QVariant();
        return nodeFor(index)->name;
    }

    QVariant headerData(int, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
            return QVariant();
        return QStringLiteral("名称");
    }

    /// Inserts \a count children into \a parent; the benchmark inserts above the
    /// viewport to check that the anchored row stays in place.
    void insertChildren(const QModelIndex &parent, int first, int count)
    {
        if (count <= 0)
            return;
        BenchTreeNode *node = nodeFor(parent);
        QVector<BenchTreeNode *> &children = node ? node->children : m_roots;
        first = qBound(0, first, children.size());
        beginInsertRows(parent, first, first + count - 1);
        for (int i = 0; i < count; ++i) {
            auto *child = new BenchTreeNode;
            child->name = QStringLiteral("inserted %1").arg(first + i);
            child->parent = node;
            children.insert(first + i, child);
        }
        endInsertRows();
    }

    bool removeChildren(const QModelIndex &parent, int first, int count)
    {
        BenchTreeNode *node = nodeFor(parent);
        QVector<BenchTreeNode *> &children = node ? node->children : m_roots;
        if (count <= 0 || first < 0 || first + count > children.size())
            return false;
        beginRemoveRows(parent, first, first + count - 1);
        for (int i = 0; i < count; ++i)
            delete children.takeAt(first);
        endRemoveRows();
        return true;
    }

    int nodeCount() const
    {
        int total = 0;
        for (BenchTreeNode *root : m_roots) {
            ++total;
            total += root->children.size();
        }
        return total;
    }

private:
    static BenchTreeNode *nodeFor(const QModelIndex &index)
    {
        return index.isValid() ? static_cast<BenchTreeNode *>(index.internalPointer()) : nullptr;
    }

    QVector<BenchTreeNode *> m_roots;
};

bool runTableScenario(const char *name, viv::VirtualTableView::MaterializationMode mode,
                      TableModel &model, int steps, int columnCount)
{
    BenchTableAdapter rowAdapter;
    BenchTableCellAdapter cellAdapter;
    viv::VirtualTableView view;
    view.setDefaultColumnWidth(90);
    view.setUniformItemHeight(24);
    view.setTableAdapter(&rowAdapter);
    view.setCellAdapter(&cellAdapter);
    view.setMaterializationMode(mode);
    // Measure the steady state without the pool cap: a wide table changes its
    // visible column count while resizing columns, and the benchmark wants to
    // show that no widget is created or destroyed by scrolling itself.
    view.recycler()->setMaxPoolSize(viv::kDefaultWidgetType, -1);
    view.resize(1000, 600);

    const quint64 baseline = processWorkingSetBytes();
    QElapsedTimer timer;
    timer.start();
    view.setModel(&model);
    view.show();
    QApplication::processEvents();
    view.flushPendingRelayout();
    const double openMs = timer.nsecsElapsed() / 1.0e6;

    const bool cellMode = mode == viv::VirtualTableView::MaterializationMode::CellWidgets;
    std::printf("\nTable: %s\n", name);
    report("open (setModel + show + layout)", openMs);
    reportCount("materialized rows", long long(view.materializedItemCount()));
    reportCount("materialized cells", long long(view.materializedCellCount()));
    reportCount("widgets created", long long(cellMode ? cellAdapter.created : rowAdapter.created));
    reportCount("visible columns (visual)", long long(view.visibleColumns().count()));
    reportMemory("working set", baseline);

    // Warm up away from the model start so that the materialization window has
    // its full overscan before the measured phase.
    view.setVerticalOffset(24 * 8);
    view.flushPendingRelayout();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    const int createdBefore = rowAdapter.created + cellAdapter.created;
    timer.restart();
    for (int step = 1; step <= steps; ++step) {
        view.setVerticalOffset(24 * (8 + step));
        view.flushPendingRelayout();
    }
    const double scrollMs = timer.nsecsElapsed() / 1.0e6;
    const int createdByVerticalScroll = rowAdapter.created + cellAdapter.created - createdBefore;
    report("vertical scroll", scrollMs);
    if (steps > 0)
        report("per scroll step", scrollMs / steps);
    reportCount("widgets created by vertical scroll", createdByVerticalScroll);

    timer.restart();
    for (int step = 1; step <= steps; ++step) {
        view.scrollByHorizontalPixels(90);
        view.flushPendingRelayout();
    }
    report("horizontal scroll", timer.nsecsElapsed() / 1.0e6);

    timer.restart();
    for (int step = 0; step < steps; ++step)
        view.setColumnWidth(step % columnCount, 80 + (step % 60));
    view.flushPendingRelayout();
    report("column resizes", timer.nsecsElapsed() / 1.0e6);

    const int createdAfter = rowAdapter.created + cellAdapter.created;
    reportCount("widgets created while interacting", createdAfter - createdBefore);
    reportCount("widgets destroyed", long long(view.destroyedWidgetCount()));
    reportCount("pooled widgets", long long(view.pooledWidgetCount()));

    const viv::VirtualViewStats stats = view.stats();
    const bool verticalFree = createdByVerticalScroll == 0;
    const bool bounded = stats.materializedItems < 400;
    // Cell Widget Mode must materialize visibleRows x visibleColumns, never
    // rows x all columns.
    const bool twoDimensional = !cellMode
        || stats.materializedItems < qMax<qsizetype>(1, view.visibleRows().count()) * qsizetype(columnCount);
    // The live widget count must stay proportional to the materialized set even
    // though column resizes change the window size.
    const bool liveBounded = stats.createCount - view.destroyedWidgetCount()
        <= 4 * qMax<qsizetype>(1, stats.materializedItems);

    std::printf("  %-42s %10s\n", "allocation free vertical scroll", verticalFree ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "materialized set bounded", bounded ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "2D virtualization (cells)", twoDimensional ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "live widget count bounded", liveBounded ? "yes" : "NO");
    return verticalFree && bounded && twoDimensional && liveBounded
        && view.destroyedWidgetCount() == 0;
}

/// Identity of the row at the top of the viewport (the scroll anchor the kernel
/// is supposed to keep in place). QPersistentModelIndex survives inserts above.
QPersistentModelIndex viewportAnchor(const viv::VirtualItemView &view)
{
    return QPersistentModelIndex(view.indexAt(QPoint(4, 1)));
}

/// Wide tree: a million visible rows already in the collapsed state, so the
/// steady-state claims (bounded widgets, allocation free scrolling, anchor
/// stability when a node above the viewport is expanded) apply to a tree too.
bool runWideTreeScenario(PathEncodedTreeModel &model, int steps, const char *label)
{
    BenchAdapter adapter;
    viv::VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(24);
    view.setOverscan(2, 2);
    view.resize(1000, 600);

    const quint64 baseline = processWorkingSetBytes();
    QElapsedTimer timer;
    timer.start();
    view.setModel(&model);
    view.show();
    QApplication::processEvents();
    view.flushPendingRelayout();
    const double openMs = timer.nsecsElapsed() / 1.0e6;

    const int createdAfterOpen = adapter.created;
    std::printf("\nTree: wide tree (%s)\n", label);
    report("open (setModel + show + layout)", openMs);
    reportCount("logical nodes (whole tree)", long long(model.nodeCount()));
    reportCount("visible rows (collapsed)", long long(view.visibleRowCount()));
    reportCount("materialized items", long long(view.materializedItemCount()));
    reportCount("widgets created", createdAfterOpen);
    reportMemory("working set", baseline);

    // ---- expand/collapse must splice the visible rows, not walk the tree ----
    view.visibilityIndex()->resetModelQueryCount();
    QModelIndex branch = model.index(0, 0);
    const qsizetype visibleCollapsed = view.visibleRowCount();
    timer.restart();
    for (int level = 0; level < 4 && branch.isValid(); ++level) {
        view.expand(branch);
        branch = model.index(0, 0, branch);
    }
    const double expandMs = timer.nsecsElapsed() / 1.0e6;
    const quint64 expandQueries = view.visibilityIndex()->modelQueryCount();
    report("expand a 4 level deep path", expandMs);
    reportCount("  model queries for those expands", long long(expandQueries));
    reportCount("  visible rows after expanding", long long(view.visibleRowCount()));

    timer.restart();
    view.collapse(model.index(0, 0));
    const double collapseMs = timer.nsecsElapsed() / 1.0e6;
    report("collapse root 0 again", collapseMs);
    const bool collapsedBack = view.visibleRowCount() == visibleCollapsed;

    // ---- steady state scrolling over the visible rows ----------------------
    view.setVerticalOffset(24 * 8);
    view.flushPendingRelayout();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const int createdBeforeScroll = adapter.created;
    const int destroyedBeforeScroll = int(view.destroyedWidgetCount());
    timer.restart();
    for (int step = 1; step <= steps; ++step) {
        view.setVerticalOffset(24 * qint64(8 + step));
        view.flushPendingRelayout();
    }
    const double scrollMs = timer.nsecsElapsed() / 1.0e6;
    const int createdByScroll = adapter.created - createdBeforeScroll;
    report("continuous scrolling", scrollMs);
    if (steps > 0)
        report("per scroll step", scrollMs / steps);
    reportCount("widgets created while scrolling", createdByScroll);
    reportCount("widgets destroyed while scrolling",
                long long(view.destroyedWidgetCount()) - destroyedBeforeScroll);

    // ---- expand/collapse above the viewport keeps the anchor --------------
    // Scroll into the middle of the content, so root 0 sits well above the
    // viewport and its children are inserted above the anchored row.
    view.setVerticalOffset(view.maximumVerticalOffset() / 2);
    view.flushPendingRelayout();
    const QPersistentModelIndex anchorBefore = viewportAnchor(view);
    const qint64 offsetBefore = view.verticalOffset();
    const qsizetype visibleBefore = view.visibleRowCount();
    // A model that fits the viewport has no anchor to keep; the scenario is then
    // reported as not applicable instead of as a failure.
    const bool anchorTestApplicable = offsetBefore > 0;
    view.expand(model.index(0, 0));
    view.flushPendingRelayout();
    const QPersistentModelIndex anchorAfterExpand = viewportAnchor(view);
    const qint64 offsetAfterExpand = view.verticalOffset();
    const qsizetype visibleAfterExpand = view.visibleRowCount();
    view.collapse(model.index(0, 0));
    view.flushPendingRelayout();
    const QPersistentModelIndex anchorAfterCollapse = viewportAnchor(view);
    const qint64 offsetAfterCollapse = view.verticalOffset();

    const bool expandKept = !anchorTestApplicable || (anchorBefore.isValid() && anchorBefore == anchorAfterExpand);
    const bool collapseKept = !anchorTestApplicable
        || (anchorBefore.isValid() && anchorBefore == anchorAfterCollapse);
    // Expanding a node keeps the expansion state of its descendants, so the
    // number of inserted rows is whatever the visible mapping grew by. For the
    // anchored row to stay at the top, the offset must move by exactly that.
    const qint64 insertedRows = qint64(visibleAfterExpand - visibleBefore);
    const bool offsetMoved = !anchorTestApplicable
        || (insertedRows > 0 && offsetAfterExpand - offsetBefore == insertedRows * 24);
    const bool offsetRestored = !anchorTestApplicable || offsetAfterCollapse == offsetBefore;

    std::printf("\nTree: anchor stability (expand/collapse above the viewport)\n");
    std::printf("  %-42s %10s\n", "anchor row before expand", qPrintable(anchorBefore.data().toString()));
    std::printf("  %-42s %10s\n", "anchor row after expand",
                qPrintable(anchorAfterExpand.data().toString()));
    reportCount("rows inserted above the anchor", insertedRows);
    reportCount("vertical offset delta (px)", long long(offsetAfterExpand - offsetBefore));
    std::printf("  %-42s %10s\n", "anchor kept while expanding", expandKept ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "anchor kept while collapsing", collapseKept ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "offset moved by the inserted rows", offsetMoved ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "offset restored by collapsing", offsetRestored ? "yes" : "NO");

    const viv::VirtualViewStats stats = view.stats();
    const bool bounded = stats.materializedItems < 200;
    const bool allocationFree = createdByScroll == 0;
    // Expanding a path visits the sub-trees along that path only: a handful of
    // rowCount()/index() queries per level, never the billions of nodes.
    const bool incremental = expandQueries < 200;

    std::printf("\n");
    std::printf("  %-42s %10s\n", "allocation free tree scrolling", allocationFree ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "materialized set bounded", bounded ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "expand/collapse is incremental", incremental ? "yes" : "NO");
    return allocationFree && bounded && incremental && expandKept && collapseKept && offsetMoved
        && offsetRestored && collapsedBack;
}

/// Heap tree: expand everything, then mutate it above the viewport. This is the
/// "no business reload()" contract of the architecture document applied to a
/// tree.
bool runHeapTreeScenario(BenchTreeModel &model, int steps)
{
    BenchAdapter adapter;
    viv::VirtualTreeView view;
    view.setAdapter(&adapter);
    view.setUniformItemHeight(24);
    view.setOverscan(2, 2);
    view.resize(1000, 600);
    view.setModel(&model);
    view.show();
    QApplication::processEvents();
    view.flushPendingRelayout();

    // Expand every branch. The loop re-scans the visible rows until the mapping
    // stops growing, so it also covers nested expansion.
    view.visibilityIndex()->resetModelQueryCount();
    QElapsedTimer timer;
    timer.start();
    for (bool grew = true; grew;) {
        grew = false;
        for (qsizetype row = 0; row < view.visibleRowCount(); ++row) {
            const QModelIndex index = view.visibilityIndex()->indexAtVisibleRow(row);
            if (index.isValid() && view.hasChildren(index) && !view.isExpanded(index)) {
                view.expand(index);
                grew = true;
            }
        }
    }
    const double expandAllMs = timer.nsecsElapsed() / 1.0e6;
    const quint64 expandAllQueries = view.visibilityIndex()->modelQueryCount();

    std::printf("\nTree: heap tree (%d nodes, fully expanded)\n", model.nodeCount());
    report("expand every branch", expandAllMs);
    reportCount("model queries while expanding everything", long long(expandAllQueries));
    reportCount("visible rows (fully expanded)", long long(view.visibleRowCount()));
    reportCount("materialized items", long long(view.materializedItemCount()));
    reportCount("widgets created", adapter.created);

    // ---- mutations above the viewport keep the anchored row ----------------
    view.setVerticalOffset(24 * 1000);
    view.flushPendingRelayout();
    const QPersistentModelIndex anchorBefore = viewportAnchor(view);
    const qsizetype visibleBefore = view.visibleRowCount();
    const int createdBefore = adapter.created;
    const qint64 offsetBefore = view.verticalOffset();

    timer.restart();
    for (int burst = 0; burst < 20; ++burst)
        model.insertChildren(QModelIndex(), 2, 10);
    view.flushPendingRelayout();
    const double insertMs = timer.nsecsElapsed() / 1.0e6;

    const QPersistentModelIndex anchorAfterInsert = viewportAnchor(view);
    const qsizetype visibleAfterInsert = view.visibleRowCount();
    const qint64 offsetAfterInsert = view.verticalOffset();
    const bool insertKept = anchorBefore.isValid() && anchorBefore == anchorAfterInsert;

    timer.restart();
    for (int burst = 0; burst < 20; ++burst)
        model.removeChildren(QModelIndex(), 2, 10);
    view.flushPendingRelayout();
    const double removeMs = timer.nsecsElapsed() / 1.0e6;

    const QPersistentModelIndex anchorAfterRemove = viewportAnchor(view);
    const bool removeKept = anchorBefore.isValid() && anchorBefore == anchorAfterRemove;
    const qsizetype visibleAfterRemove = view.visibleRowCount();

    std::printf("\nTree: model mutations above the viewport (%d scroll steps)\n", steps);
    report("20 x insert 10 children", insertMs);
    report("20 x remove 10 children", removeMs);
    reportCount("visible rows added", long long(visibleAfterInsert - visibleBefore));
    reportCount("visible rows after removing", long long(visibleAfterRemove));
    reportCount("widgets created by the mutations", adapter.created - createdBefore);
    reportCount("widgets destroyed", long long(view.destroyedWidgetCount()));
    std::printf("  %-42s %10s\n", "anchor before", qPrintable(anchorBefore.data().toString()));
    std::printf("  %-42s %10s\n", "anchor after inserting",
                qPrintable(anchorAfterInsert.data().toString()));
    std::printf("  %-42s %10s\n", "anchor after removing",
                qPrintable(anchorAfterRemove.data().toString()));
    reportCount("offset before", long long(offsetBefore));
    reportCount("offset after inserting", long long(offsetAfterInsert));
    reportCount("offset after removing", long long(view.verticalOffset()));
    std::printf("  %-42s %10s\n", "anchor kept while inserting", insertKept ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "anchor kept while removing", removeKept ? "yes" : "NO");

    // ---- steady state scrolling over the fully expanded tree ---------------
    const int createdBeforeScroll = adapter.created;
    timer.restart();
    for (int step = 1; step <= steps; ++step) {
        view.setVerticalOffset(24 * qint64(step));
        view.flushPendingRelayout();
    }
    const double scrollMs = timer.nsecsElapsed() / 1.0e6;
    const int createdByScroll = adapter.created - createdBeforeScroll;
    report("scrolling over the expanded tree", scrollMs);
    reportCount("widgets created while scrolling", createdByScroll);

    const viv::VirtualViewStats stats = view.stats();
    const bool bounded = stats.materializedItems < 200;
    // Expanding a tree of this size touches every branch once: the query count
    // must stay proportional to the model, never depth x model.
    const bool proportional = expandAllQueries < quint64(model.nodeCount()) * 4;
    reportCount("model nodes (after mutations)", long long(model.nodeCount()));
    const bool mutationsRecovered = visibleAfterRemove == visibleBefore;

    std::printf("\n");
    std::printf("  %-42s %10s\n", "materialized set bounded", bounded ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "expand cost proportional to model", proportional ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "allocation free tree scrolling", createdByScroll == 0 ? "yes" : "NO");
    std::printf("  %-42s %10s\n", "insert/remove leaves the mapping", mutationsRecovered ? "yes" : "NO");
    return bounded && proportional && insertKept && removeKept && mutationsRecovered
        && createdByScroll == 0;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // The benchmark prints a few dozen progress lines; unbuffered stdout makes
    // them visible when the output is a pipe (CI, editors, crash/hang triage).
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    QCommandLineParser parser;
    parser.setApplicationDescription("VirtualItemViews list benchmark");
    parser.addHelpOption();
    QCommandLineOption rowsOption(QStringLiteral("rows"), QStringLiteral("Logical row count"),
                                  QStringLiteral("count"), QStringLiteral("1000000"));
    QCommandLineOption stepsOption(QStringLiteral("steps"), QStringLiteral("Scroll steps"),
                                   QStringLiteral("count"), QStringLiteral("2000"));
    QCommandLineOption stepSizeOption(QStringLiteral("step-size"), QStringLiteral("Pixels per step"),
                                      QStringLiteral("pixels"), QStringLiteral("97"));
    QCommandLineOption compareOption(QStringLiteral("compare"),
                                     QStringLiteral("Also measure a QListView with a plain delegate"));
    QCommandLineOption tableOption(QStringLiteral("table"),
                                   QStringLiteral("Measure the table (row widget mode and cell widget mode)"));
    QCommandLineOption tableColumnsOption(QStringLiteral("table-columns"),
                                          QStringLiteral("Table column count"),
                                          QStringLiteral("count"), QStringLiteral("100"));
    QCommandLineOption treeOption(QStringLiteral("tree"),
                                  QStringLiteral("Measure the tree (wide tree and mutation scenarios)"));
    QCommandLineOption treeRootsOption(QStringLiteral("tree-roots"),
                                       QStringLiteral("Top level node count of the wide tree"),
                                       QStringLiteral("count"), QStringLiteral("1000000"));
    QCommandLineOption treeBranchingOption(QStringLiteral("tree-branching"),
                                           QStringLiteral("Children per node of the wide tree"),
                                           QStringLiteral("count"), QStringLiteral("10"));
    QCommandLineOption treeDepthOption(QStringLiteral("tree-depth"),
                                       QStringLiteral("Levels below the top level of the wide tree"),
                                       QStringLiteral("count"), QStringLiteral("4"));
    parser.addOption(rowsOption);
    parser.addOption(stepsOption);
    parser.addOption(stepSizeOption);
    parser.addOption(compareOption);
    parser.addOption(tableOption);
    parser.addOption(tableColumnsOption);
    parser.addOption(treeOption);
    parser.addOption(treeRootsOption);
    parser.addOption(treeBranchingOption);
    parser.addOption(treeDepthOption);
    parser.process(app);

    const int rowCount = parser.value(rowsOption).toInt();
    const int steps = parser.value(stepsOption).toInt();
    const int stepSize = parser.value(stepSizeOption).toInt();

    std::printf("VirtualItemViews benchmark\n");
    reportCount("logical rows", rowCount);
    reportCount("scroll steps", steps);
    reportCount("pixels per step", stepSize);
    std::printf("\n");

    BenchModel model(rowCount);
    const quint64 baselineMemory = processWorkingSetBytes();
    reportMemory("working set after building the model", 0);

    BenchAdapter adapter;
    viv::VirtualListView view;
    view.resize(800, 600);
    view.setAdapter(&adapter);
    view.setUniformItemHeight(24);
    view.setOverscan(2, 2);

    QElapsedTimer openTimer;
    openTimer.start();
    view.setModel(&model);
    view.show();
    QApplication::processEvents();
    view.flushPendingRelayout();
    const double openMs = openTimer.nsecsElapsed() / 1.0e6;

    report("open (setModel + show + layout)", openMs);
    const int createdAfterOpen = adapter.created;
    reportCount("widgets created for the initial viewport", createdAfterOpen);
    reportCount("materialized items", view.materializedItemCount());
    reportMemory("working set with the view open", baselineMemory);

    // ---- scenario 1: continuous scrolling (the A2 acceptance case) ---------
    // Stay away from both ends of the model so that the materialized window
    // keeps its full size (touching the model edges legitimately shrinks it).
    const int scrollEnd = qMax(1, view.verticalScrollBar()->maximum());
    const int safeStart = scrollEnd / 4;
    const int safeEnd = qMax(safeStart + 1, scrollEnd - 1200);
    int value = safeStart;
    view.verticalScrollBar()->setValue(value);
    view.flushPendingRelayout();
    const int createdBeforeScroll = adapter.created;
    QElapsedTimer timer;
    timer.start();
    for (int step = 1; step <= steps; ++step) {
        value += stepSize;
        if (value > safeEnd)
            value = safeStart;
        view.verticalScrollBar()->setValue(value);
        view.flushPendingRelayout();
    }
    const double scrollMs = timer.nsecsElapsed() / 1.0e6;
    const int createdContinuous = adapter.created - createdBeforeScroll;

    std::printf("\nContinuous scrolling (sequential steps)\n");
    report("total", scrollMs);
    if (steps > 0)
        report("per step", scrollMs / steps);
    reportCount("widgets created while scrolling", createdContinuous);
    reportCount("widgets destroyed", long long(view.destroyedWidgetCount()));
    reportCount("pooled widgets", long long(view.pooledWidgetCount()));
    reportCount("live widgets", long long(adapter.created - int(view.destroyedWidgetCount())));
    reportCount("materialized items", view.materializedItemCount());

    // ---- scenario 2: random jumps (boundary crossings may grow the pool) ---
    const int createdBeforeJumps = adapter.created;
    std::mt19937 rng(20260923);
    const int maximum = qMax(1, view.verticalScrollBar()->maximum());
    for (int step = 0; step < steps; ++step) {
        view.verticalScrollBar()->setValue(int(rng() % unsigned(maximum)));
        view.flushPendingRelayout();
    }
    const int createdDuringJumps = adapter.created - createdBeforeJumps;
    std::printf("\nRandom scrollbar jumps\n");
    reportCount("widgets created during jumps", createdDuringJumps);
    reportCount("widgets destroyed", long long(view.destroyedWidgetCount()));
    reportCount("materialized items", view.materializedItemCount());

    const bool widgetsStable = createdContinuous == 0;
    // A jump into a smaller window (top/bottom of the model) may have to grow the
    // pool back, but never proportionally to the number of steps.
    const bool countBounded = view.materializedItemCount() < 200;
    const bool jumpsBounded = createdDuringJumps <= 64;
    const bool nothingDestroyed = view.destroyedWidgetCount() == 0;

    // ---- scenario 3: model mutation latency --------------------------------
    QElapsedTimer mutationTimer;
    mutationTimer.start();
    for (int i = 0; i < 2000; ++i)
        model.setRowText((i * 7) % model.rowCount());
    view.flushPendingRelayout();
    const double dataChangedMs = mutationTimer.nsecsElapsed() / 1.0e6;

    mutationTimer.restart();
    for (int burst = 0; burst < 20; ++burst)
        model.insertRows(model.rowCount() / 2, 500);
    view.flushPendingRelayout();
    const double insertMs = mutationTimer.nsecsElapsed() / 1.0e6;

    mutationTimer.restart();
    for (int burst = 0; burst < 20; ++burst)
        model.removeRows(model.rowCount() / 2, 500);
    view.flushPendingRelayout();
    const double removeMs = mutationTimer.nsecsElapsed() / 1.0e6;

    mutationTimer.restart();
    for (int i = 0; i < 60; ++i) {
        view.resize(800, 400 + (i % 6) * 60);
        view.flushPendingRelayout();
    }
    const double resizeMs = mutationTimer.nsecsElapsed() / 1.0e6;

    std::printf("\nModel mutation latency (including the coalesced relayout)\n");
    report("2000 x dataChanged (single row)", dataChangedMs);
    report("20 x insert 500 rows", insertMs);
    report("20 x remove 500 rows", removeMs);
    report("60 x resize relayout", resizeMs);
    reportCount("logical rows after mutations", model.rowCount());
    reportCount("widgets created", adapter.created);
    reportCount("widgets destroyed", long long(view.destroyedWidgetCount()));
    reportMemory("working set after mutations", baselineMemory);

    std::printf("\n");
    std::printf("  allocation free continuous scroll : %s\n", widgetsStable ? "yes" : "NO");
    std::printf("  random jumps only grow the pool  : %s\n", jumpsBounded ? "yes" : "NO");
    std::printf("  no widget destroyed while scrolling: %s\n", nothingDestroyed ? "yes" : "NO");
    std::printf("  materialized count bounded       : %s\n", countBounded ? "yes" : "NO");

    int result = 0;
    if (!widgetsStable || !countBounded || !jumpsBounded || !nothingDestroyed) {
        std::printf("\nFAILED: the virtualization invariants were violated.\n");
        result = 1;
    } else {
        std::printf("\nOK\n");
    }

    if (parser.isSet(compareOption)) {
        std::printf("\nReference: QListView with a plain string model\n");
        QListView plainView;
        auto *plainModel = new BenchModel(rowCount, &plainView);
        plainView.setModel(plainModel);
        plainView.resize(800, 600);
        QElapsedTimer referenceTimer;
        referenceTimer.start();
        plainView.show();
        QApplication::processEvents();
        report("open", referenceTimer.nsecsElapsed() / 1.0e6);
        reportMemory("working set with QListView open", baselineMemory);
        referenceTimer.restart();
        for (int step = 1; step <= steps; ++step) {
            const int value = (step * stepSize) % qMax(1, plainView.verticalScrollBar()->maximum());
            plainView.verticalScrollBar()->setValue(value);
        }
        QApplication::processEvents();
        report("scroll", referenceTimer.nsecsElapsed() / 1.0e6);

        std::printf("\nReference: QListWidget + setItemWidget (extreme, %d rows)\n",
                    qMin(2000, rowCount));
        const int widgetRows = qMin(2000, rowCount);
        QListWidget listWidget;
        listWidget.resize(800, 600);
        referenceTimer.restart();
        listWidget.show();
        for (int row = 0; row < widgetRows; ++row) {
            auto *item = new QListWidgetItem();
            item->setSizeHint(QSize(400, 24));
            listWidget.addItem(item);
            listWidget.setItemWidget(item, new BenchRowWidget(&listWidget));
        }
        QApplication::processEvents();
        report("populate + open", referenceTimer.nsecsElapsed() / 1.0e6);
        reportMemory("working set with QListWidget open", baselineMemory);
    }

    if (parser.isSet(tableOption)) {
        const int tableColumns = qMax(1, parser.value(tableColumnsOption).toInt());
        TableModel tableModel(rowCount, tableColumns);
        std::printf("\n=== Table benchmark (%d rows x %d columns) ===\n", rowCount, tableColumns);

        const bool rowModeOk = runTableScenario("row widget mode", 
                                               viv::VirtualTableView::MaterializationMode::RowWidgets,
                                               tableModel, qMin(steps, 400), tableColumns);
        const bool cellModeOk = runTableScenario("cell widget mode",
                                                 viv::VirtualTableView::MaterializationMode::CellWidgets,
                                                 tableModel, qMin(steps, 400), tableColumns);
        if (!rowModeOk || !cellModeOk) {
            std::printf("\nFAILED: table invariants were violated.\n");
            result = 1;
        } else {
            std::printf("\nOK (table)\n");
        }
    }

    if (parser.isSet(treeOption)) {
        const int treeRoots = qMax(1, parser.value(treeRootsOption).toInt());
        const int treeBranching = qMax(1, parser.value(treeBranchingOption).toInt());
        const int treeDepth = qMax(1, parser.value(treeDepthOption).toInt());
        PathEncodedTreeModel wideTree(treeRoots, treeBranching, treeDepth);
        const QString treeLabel = QStringLiteral("%1 roots x %2 children, depth %3")
                                      .arg(treeRoots)
                                      .arg(treeBranching)
                                      .arg(treeDepth);
        const QByteArray treeLabelBytes = treeLabel.toLocal8Bit();
        std::printf("\n=== Tree benchmark (%s) ===\n", treeLabelBytes.constData());

        const bool wideOk = runWideTreeScenario(wideTree, qMin(steps, 500), treeLabelBytes.constData());
        // The heap tree is small on purpose: the mutation scenario is about the
        // anchor contract, not about throughput.
        BenchTreeModel heapTree(200, 20);
        const bool heapOk = runHeapTreeScenario(heapTree, qMin(steps, 400));
        if (!wideOk || !heapOk) {
            std::printf("\nFAILED: tree invariants were violated.\n");
            result = 1;
        } else {
            std::printf("\nOK (tree)\n");
        }
    }

    return result;
}
