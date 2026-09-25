#pragma once

#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/nativeheaderview.h>
#include <virtualitemviews/tablespan.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualitemview.h>

#include <QHash>
#include <QPersistentModelIndex>
#include <QSet>
#include <QVector>

class QAbstractItemModel;
class QKeyEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

namespace viv {

class ListLayout;

/// Table MVP (architecture document §43 v0.4) on top of the virtualization
/// kernel.
///
/// Design invariants:
///  - HeaderGeometry is the single source of truth for column geometry and
///    state; the body, the native header and the business row widgets all query
///    it (§14, §45.10),
///  - rows are virtualized exactly like VirtualListView: only visible rows,
///    overscan rows and pinned rows own a QWidget,
///  - every materialized row is one business QWidget (Row Widget Mode) whose
///    columns are positioned by ColumnHost or by the adapter hook, so no
///    cell-level QWidget is created (§45.8).
///
/// Cell Widget Mode with 2D (row x column) virtualization arrives with v0.5.
class VirtualTableView : public VirtualItemView
{
    Q_OBJECT

public:
    using ItemHeightMode = VirtualItemView::ItemHeightMode;
    using SelectionBehavior = VirtualItemView::SelectionBehavior;
    using SelectionMode = VirtualItemView::SelectionMode;
    using WheelScrollMode = VirtualItemView::WheelScrollMode;

    /// How an explicitly resized row height interacts with measurement (§30).
    enum class RowSizePolicy {
        ExplicitWins,
        MeasuredWins,
    };
    Q_ENUM(RowSizePolicy)

    /// How the table body is materialized (architecture document §28).
    enum class MaterializationMode {
        /// One QWidget per materialized row (Row Widget Mode, default).
        RowWidgets,
        /// One QWidget per visible cell: visibleRows x visibleColumns.
        CellWidgets,
    };
    Q_ENUM(MaterializationMode)

    explicit VirtualTableView(QWidget *parent = nullptr);
    ~VirtualTableView() override;

    void setModel(QAbstractItemModel *model) override;

    // -- headers -------------------------------------------------------------
    HeaderGeometry *horizontalHeaderGeometry() const { return m_columns; }
    HeaderGeometry *verticalHeaderGeometry() const { return m_rowHeaders; }
    /// Takes ownership of \a header; nullptr selects the default native header.
    void setHorizontalHeader(HeaderViewInterface *header);
    HeaderViewInterface *horizontalHeader() const { return m_horizontalHeader; }
    void setVerticalHeader(HeaderViewInterface *header);
    HeaderViewInterface *verticalHeader() const { return m_verticalHeader; }
    void setHorizontalHeaderVisible(bool visible);
    bool isHorizontalHeaderVisible() const { return m_horizontalHeaderVisible; }
    void setVerticalHeaderVisible(bool visible);
    bool isVerticalHeaderVisible() const { return m_verticalHeaderVisible; }
    void setHeaderHeight(int height);
    int headerHeight() const { return m_headerHeight; }
    void setVerticalHeaderWidth(int width);
    int verticalHeaderWidth() const { return m_verticalHeaderWidth; }

    // -- geometry ------------------------------------------------------------
    int columnCount() const;
    ColumnGeometry columnGeometry(int logicalIndex) const;
    int columnWidth(int logicalIndex) const;
    /// Logical column under a viewport x, honoring the panes (§31/§43): a frozen
    /// column is hit by its own viewport x, a scrollable one by the horizontal
    /// offset of its own scroll group, and a column is only hit inside the pane it
    /// belongs to (a pane is a hard boundary, also between two scrolling groups).
    /// -1 when no column is under \a viewportX.
    int columnAtViewportX(int viewportX) const;
    /// Visible columns as visual indices (hidden ones included in the range).
    VisibleRange visibleColumns() const;
    /// Visible rows (without overscan).
    VisibleRange visibleRows() const { return visibleItemRange(); }
    /// Logical indices of the columns intersecting the viewport, left to right.
    QVector<int> visibleColumnLogicalIndexes() const;
    void setColumnOverscan(int columns);
    int columnOverscan() const { return m_columnOverscan; }

    // -- column state (all writes go to HeaderGeometry) -----------------------
    void setColumnWidth(int logicalIndex, int width);
    void setColumnHidden(int logicalIndex, bool hidden);
    bool isColumnHidden(int logicalIndex) const;
    /// Moves \a fromLogicalIndex to the visual position of \a toLogicalIndex.
    ///
    /// The change is applied immediately: a programmatic reorder never animates by
    /// itself, so this changes the order (and the body) at once. Pass
    /// MoveAnimation::Animate to ask the header renderers for their visual transition
    /// as well - the committed geometry is final right away, only the header slides to
    /// it (§23). Renderers without their own section placement (a native QHeaderView)
    /// ignore that and show the new order at once.
    enum class MoveAnimation {
        Immediate,
        Animate,
    };
    Q_ENUM(MoveAnimation)
    void moveColumn(int fromLogicalIndex, int toLogicalIndex,
                    MoveAnimation animation = MoveAnimation::Immediate);
    void setDefaultColumnWidth(int width);
    int defaultColumnWidth() const;
    void setColumnMinimumWidth(int width);
    void setColumnMaximumWidth(int width);
    void setStretchLastColumn(bool stretch);
    bool stretchLastColumn() const;

    // -- horizontal scrolling -------------------------------------------------
    /// Offset of the primary scroll group (group 0): the group the header
    /// geometry and the horizontal scroll bar drive.
    qint64 horizontalOffset() const;
    void setHorizontalOffset(qint64 offset);
    void scrollByHorizontalPixels(qint64 pixels);
    qint64 maximumHorizontalOffset() const;
    qint64 horizontalContentExtent() const;
    void setHorizontalWheelPixels(int pixels);
    int horizontalWheelPixels() const { return m_horizontalWheelPixels; }

    // -- frozen columns (§31) ------------------------------------------------
    /// Freezes columns at the left/right edge: they stay visible while the
    /// scrollable pane scrolls, so the horizontal offset never applies to them.
    /// All panes derive from the same HeaderGeometry - a frozen column has no
    /// width, order or visibility copy of its own. A column in both sets stays
    /// on the left.
    void setFrozenColumns(const QVector<int> &logicalColumns);
    void setFrozenRightColumns(const QVector<int> &logicalColumns);
    void clearFrozenColumns();
    QVector<int> frozenColumns() const { return m_panes.frozenColumns(); }
    QVector<int> frozenRightColumns() const { return m_panes.frozenRightColumns(); }
    bool isColumnFrozen(int logicalIndex) const { return m_panes.isFrozenColumn(logicalIndex); }
    /// Current panes: frozen left / scrollable / frozen right (§31).
    QVector<TablePane> panes() const { return m_panes.panes(); }
    TablePane::Type paneTypeForColumn(int logicalIndex) const
    {
        return m_panes.paneOfColumn(logicalIndex);
    }
    /// Explicit pane list (§43 "advanced panes"): panes in visual order, each
    /// with its own columns and scroll group. An empty list restores the default
    /// "frozen left | scrollable | frozen right" layout, so setFrozenColumns()
    /// stays the shorthand for the common case.
    ///
    /// Any number of frozen panes and scrolling groups is supported. Panes of one
    /// scroll group share a horizontal offset; the group of the *first* scrolling
    /// pane is the primary group and is the one the header geometry and the
    /// horizontal scroll bar drive, so keep that pane in group 0 to have the
    /// scroll bar control it. Every other group is driven by
    /// setHorizontalOffset(group, offset).
    void setPanes(const QVector<TablePaneSpec> &panes);
    QVector<TablePaneSpec> paneSpecs() const { return m_panes.paneSpecs(); }
    /// Index of the pane that shows \a logicalIndex (-1 when hidden/unknown).
    int paneIndexOfColumn(int logicalIndex) const { return m_panes.paneIndexOfColumn(logicalIndex); }
    /// Scroll groups of the current layout, ascending.
    QVector<int> scrollGroups() const { return m_panes.scrollGroups(); }
    /// Scroll group of the pane the header geometry and the scroll bar drive
    /// (the first scrolling pane of the list; -1 when nothing scrolls).
    int primaryScrollGroup() const { return m_panes.primaryScrollGroup(); }
    /// Horizontal offset of one scroll group. The primary group follows the header
    /// geometry, so setHorizontalOffset(qint64) moves it.
    qint64 horizontalOffset(int scrollGroup) const { return m_panes.groupOffset(scrollGroup); }
    /// Sets the offset of \a scrollGroup (no-op for the primary group: use
    /// setHorizontalOffset(qint64) there, the scroll bar drives it).
    void setHorizontalOffset(int scrollGroup, qint64 offset);
    qint64 maximumHorizontalOffset(int scrollGroup) const
    {
        return m_panes.maximumGroupOffset(scrollGroup);
    }
    /// Geometry of the pane boundary lines, left to right (diagnostics/tests;
    /// there is one per visible pane boundary).
    QVector<QRect> paneSeparatorRects() const;
    /// Look of the line that separates two panes, in the header *and* in the
    /// body. The default is a 1 px line in the colour the current style paints
    /// section separators with; setting an explicit colour, a different width
    /// (0 hides the line) or a pen style applies to both.
    void setPaneSeparatorStyle(const PaneSeparatorStyle &style);
    PaneSeparatorStyle paneSeparatorStyle() const { return m_paneSeparatorStyle; }

    // -- header animation (§23/§24) ------------------------------------------
    /// Visual geometry animation of the installed header renderers: a section move
    /// slides the section to its committed position instead of teleporting, while
    /// the body keeps reading the committed HeaderGeometry (it jumps once, at the
    /// commit). Resizing, scrolling and pane changes stay immediate, because there
    /// the body follows every frame. Renderers without their own section placement
    /// (a native QHeaderView) ignore the setting. Default: enabled, 160 ms.
    void setHeaderAnimationEnabled(bool enabled);
    bool headerAnimationEnabled() const { return m_headerAnimationEnabled; }
    /// Duration of the visual transition in milliseconds; 0 turns it off.
    void setHeaderAnimationDuration(int ms);
    int headerAnimationDuration() const { return m_headerAnimationDuration; }

    // -- row heights ---------------------------------------------------------
    void setRowSizePolicy(RowSizePolicy policy);
    RowSizePolicy rowSizePolicy() const { return m_rowSizePolicy; }
    int rowHeight(qsizetype row) const;
    /// Explicit (user) row height; wins over measurement by default.
    void setRowHeight(qsizetype row, int height);
    void clearRowHeight(qsizetype row);
    bool hasExplicitRowHeight(qsizetype row) const;

    // -- spans (§43 "spans", see docs/spans.md) ------------------------------
    /// Source of the merged cells. The default is "nothing is merged", so a
    /// table without spans behaves exactly as before. Passing nullptr detaches
    /// (and with takeOwnership = true deletes) the current provider.
    void setSpanProvider(TableSpanProvider *provider, bool takeOwnership = false);
    TableSpanProvider *spanProvider() const { return m_spanProvider; }
    /// Convenience for the map provider: merges the cells starting at
    /// (\a row, \a column). Creates the owned TableSpanMap on first use.
    void setSpan(int row, int column, int rowSpan = 1, int columnSpan = 1);
    /// Drops the span anchored at (\a row, \a column).
    void removeSpan(int row, int column);
    void clearSpans();
    /// Span whose anchor is \a index (1x1 when the cell is not an anchor).
    TableSpan spanAt(const QModelIndex &index) const;
    /// Anchor of the merged area containing \a index (the index itself when the
    /// cell is not merged, and when there is no provider at all).
    QModelIndex anchorIndex(const QModelIndex &index) const;
    /// True when \a index is covered by another cell's span.
    bool isSpanCovered(const QModelIndex &index) const;
    /// Merged rectangle (viewport coordinates) whose anchor is \a index, derived
    /// from the committed column geometry and the row layout. Clamped to the
    /// model and to the anchor's pane: a span never crosses a pane (§31).
    QRect spanRect(const QModelIndex &index) const;
    /// Rect of a single cell (viewport coordinates), span aware: the merged
    /// rectangle for an anchor, empty for a covered cell.
    QRect cellRect(const QModelIndex &index) const;

    // -- sorting -------------------------------------------------------------
    void setSortingEnabled(bool enabled);
    bool isSortingEnabled() const { return m_sortingEnabled; }
    void sortByColumn(int logicalIndex, Qt::SortOrder order);
    void setSortIndicator(int logicalIndex, Qt::SortOrder order);

    // -- persistence ---------------------------------------------------------
    QByteArray saveHeaderState() const;
    bool restoreHeaderState(const QByteArray &state);

    // -- adapter -------------------------------------------------------------
    void setTableAdapter(TableWidgetAdapter *adapter, bool takeOwnership = false);
    TableWidgetAdapter *tableAdapter() const;

    void setMaterializationMode(MaterializationMode mode);
    MaterializationMode materializationMode() const { return m_materializationMode; }
    /// Public query: Cell Widget Mode does not materialize row widgets.
    bool usesItemWidgets() const override;
    /// Public query: in Cell Widget Mode the stats report cells.
    VirtualViewStats stats() const override;

    /// Adapter of the cell widgets (Cell Widget Mode only).
    void setCellAdapter(CellWidgetAdapter *adapter, bool takeOwnership = false);
    CellWidgetAdapter *cellAdapter() const { return m_cellAdapter; }

    /// Cell diagnostics (Cell Widget Mode).
    qsizetype materializedCellCount() const { return m_cells.size(); }
    QWidget *cellWidget(const QModelIndex &index) const;
    QModelIndex cellIndexForWidget(const QWidget *widget) const;
    QList<QModelIndex> materializedCellIndexes() const;

signals:
    void sortIndicatorRequested(int logicalIndex, Qt::SortOrder order);
    void horizontalOffsetChanged(qint64 offset);
    void columnGeometryChanged();
    void rowHeightChanged(qsizetype row, int height);

protected:
    qsizetype viewItemCount() const override;
    QModelIndex viewIndex(qsizetype item, int column = 0) const override;
    qsizetype viewItemForIndex(const QModelIndex &index) const override;
    bool isLayoutParent(const QModelIndex &parent) const override;
    QModelIndex indexForNavigation(qsizetype item, const QModelIndex &current) const override;
    void materializeItems(const VisibleRange &rows) override;
    /// Cell Widget Mode materializes every range the kernel asks for: the scrolling
    /// window plus the frozen rows (§31 row direction).
    void materializeItemRanges(const QVector<VisibleRange> &ranges) override;
    void rebindItemsInRange(const QModelIndex &topLeft, const QModelIndex &bottomRight) override;
    QModelIndex indexAt(const QPoint &viewportPos) const override;
    bool canMeasureItem(qsizetype item) const override;
    void afterMaterialize() override;
    bool handleItemKeyPress(QKeyEvent *event) override;
    /// §38: a table drops between rows (row semantics) or into a cell. The
    /// column of the target is -1 when the whole row is the drop unit.
    VirtualItemView::DropTarget resolveDropTarget(const QPoint &viewportPos) const override;
    QRect resolveDropIndicatorRect(const DropTarget &target) const override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void ensureHeaders();
    void adoptHeader(HeaderViewInterface *&current, bool &owns, HeaderViewInterface *replacement,
                     HeaderGeometry *geometry, Qt::Orientation orientation);
    void layoutHeaderWidgets();
    void connectColumnSignals(QAbstractItemModel *model);
    void onColumnsInserted(const QModelIndex &parent, int first, int last);
    void onColumnsRemoved(const QModelIndex &parent, int first, int last);
    void onColumnsMoved(const QModelIndex &parent, int start, int end,
                        const QModelIndex &destinationParent, int destinationColumn);
    void onHeaderGeometryChanged();
    void onVerticalSectionResized(int logicalIndex, int oldSize, int newSize);
    /// A user drag on the row-number strip: the row height becomes explicit.
    void onVerticalHeaderUserResized(int row, int oldSize, int newSize);
    /// Keeps the row-number strip aligned with the body while scrolling.
    void updateRowHeaderOffset();
    void syncHorizontalScrollBar();
    void updateColumnLayout();
    /// Recomputes the pane layout (§31) and the column layout that depends on
    /// it (frozen widths change the scrollable range and every column x).
    void updatePaneLayout();
    /// Creates/destroys/updates the frozen pane header renderers.
    void syncHeaderPanes();
    /// Creates/positions the 1 px body lines of the pane boundaries.
    void syncPaneSeparatorLines();
    /// Pane header of the same kind as the installed horizontal header.
    HeaderViewInterface *createHorizontalPaneHeader();
    /// Pushes the animation settings (§23/§24) into every header renderer.
    void applyHeaderAnimationSettings();
    /// Asks every header renderer for a visual transition (or clears the request)
    /// around a programmatic section move (§23).
    void requestSectionMoveAnimation(bool animated);
    /// Lifts the body lines above the (re)materialized items.
    void raisePaneSeparatorLines();
    /// Column hosts of a row widget (direct children plus the clip host's).
    QList<ColumnHost *> rowColumnHosts(QWidget *rowWidget) const;
    /// Clip containers of a row widget, one per scrolling pane (empty when the
    /// panes do not need clipping: a single pane covers the whole viewport).
    QVector<QWidget *> rowPaneClipHosts(QWidget *rowWidget) const;
    /// Clip container of \a paneIndex inside a row widget, or null.
    QWidget *rowPaneClipHost(QWidget *rowWidget, int paneIndex) const;
    /// Clip container of one pane inside a row widget (§31/§43): created on
    /// demand and positioned on the pane's rectangle in row widget coordinates.
    /// Several panes scroll independently, so every scrolling pane has its own.
    QWidget *ensureRowPaneClipHost(QWidget *rowWidget, int paneIndex, const QRect &paneLocalRect);
    /// Drops the clip containers of \a rowWidget that no longer belong to a
    /// scrolling pane (nothing is frozen or the pane list changed) and hands their
    /// column hosts back to the row widget. The containers are found through the
    /// row widget's children (they carry their pane index), so no widget pointer is
    /// ever kept on the side.
    void dropStaleRowPaneClipHosts(QWidget *rowWidget, const QSet<int> &scrollingPanes);
    /// True while the pane layout has more than one pane, i.e. while a scrolling
    /// pane has to be clipped against its neighbours.
    bool panesNeedClipping() const { return m_panes.panes().size() > 1; }
    /// Creates/destroys/positions the clip container of every scrolling pane in
    /// Cell Widget Mode (§43).
    void syncCellPaneClipHosts();
    /// Key of the cell clip container of one (row pane, column pane) pair: with
    /// frozen rows the cells are clipped in both directions, so the container is an
    /// intersection of a row pane and a column pane (§31).
    static quint64 cellClipKey(ItemPane::Type rowPane, int columnPaneIndex);
    void applyColumnLayout(const MaterializedItem &item);
    /// Layout context of one row. \a rowIndex is the row being laid out; it is
    /// what makes the span decisions of that row available to the adapter
    /// (§43 "spans"). Without a row index the span context stays empty.
    /// \a paneHosts maps a pane index to the framework clip container of that
    /// pane; adapters that lay their own children out use it to clip them the
    /// same way the ColumnHost logic does (§43 "advanced panes").
    TableRowLayoutContext layoutContext(const QRect &viewportRect,
                                        const QHash<int, QWidget *> &paneHosts = QHash<int, QWidget *>(),
                                        const QModelIndex &rowIndex = QModelIndex()) const;
    void updateRowHeaderGeometry();
    void scrollToColumn(int logicalIndex);
    void onSortIndicatorChanged(int logicalIndex, Qt::SortOrder order);

    QVector<int> columnsForCellMaterialization() const;
    QRect cellRect(qsizetype row, int logicalColumn) const;
    QWidget *createCellWidget(const QPersistentModelIndex &index);
    void recycleCell(const QPersistentModelIndex &index, QWidget *widget);
    void recycleAllCells();
    bool isCellPinned(const QPersistentModelIndex &index, const QWidget *widget) const;
    void updateCellGeometry();

    ListLayout *m_rowLayout = nullptr;
    HeaderGeometry *m_columns = nullptr;
    TablePaneLayout m_panes;
    HeaderGeometry *m_rowHeaders = nullptr;
    HeaderViewInterface *m_horizontalHeader = nullptr;
    HeaderViewInterface *m_verticalHeader = nullptr;
    /// Header renderer of every non-primary pane, indexed by pane index (§43):
    /// a pane shows its own columns at its own viewport x, so it needs its own
    /// renderer of the same geometry. The primary pane uses m_horizontalHeader.
    QVector<HeaderViewInterface *> m_paneHeaders;
    /// Framework containers that clip the cell widgets, keyed by
    /// cellClipKey(row pane, column pane) (§43 "advanced panes" + §31 row
    /// direction: several groups scroll independently, so each pane intersection
    /// has its own).
    QHash<quint64, QWidget *> m_cellClipHosts;
    /// 1 px body lines at the pane boundaries (left | scrollable | right).
    QVector<QWidget *> m_paneSeparatorLines;
    PaneSeparatorStyle m_paneSeparatorStyle;
    bool m_headerAnimationEnabled = true;
    int m_headerAnimationDuration = 160;
    // The clip containers of a row widget are its children, tagged with their pane
    // index (see PaneClipHost): a recycled row widget can never leave a stale
    // pointer behind.
    bool m_ownHorizontalHeader = false;
    bool m_ownVerticalHeader = false;
    TableWidgetAdapter *m_tableAdapter = nullptr;
    bool m_ownTableAdapter = false;
    CellWidgetAdapter *m_cellAdapter = nullptr;
    bool m_ownCellAdapter = false;
    MaterializationMode m_materializationMode = MaterializationMode::RowWidgets;
    QHash<QPersistentModelIndex, QWidget *> m_cells;
    QHash<QWidget *, WidgetType> m_cellTypes;
    bool m_cellMaterializationActive = false;

    int m_headerHeight = 28;
    int m_verticalHeaderWidth = 56;
    bool m_horizontalHeaderVisible = true;
    bool m_verticalHeaderVisible = true;
    int m_columnOverscan = 1;
    int m_horizontalWheelPixels = 48;
    TableSpanProvider *m_spanProvider = nullptr;
    bool m_ownSpanProvider = false;
    /// Set once when a row span could not be honoured (diagnostics).
    bool m_spanWarningShown = false;
    RowSizePolicy m_rowSizePolicy = RowSizePolicy::ExplicitWins;
    bool m_sortingEnabled = false;
    bool m_sortGuard = false;
    QHash<QPersistentModelIndex, int> m_explicitRowHeights;
    bool m_verticalHeaderDisabled = false;
    bool m_columnUpdateActive = false;
    bool m_rowHeaderUpdateActive = false;
    bool m_headersLaidOut = false;
};

} // namespace viv
