#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/headerview.h>
#include <virtualitemviews/scrollmapper.h>
#include <virtualitemviews/tablespan.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualitemview.h>

#include <QHash>
#include <QPersistentModelIndex>
#include <QSet>
#include <QVector>
#include <functional>

class QAbstractItemModel;
class QPainter;
class QKeyEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

namespace viv {

class VirtualHeaderView;

class ListLayout;

/// Table MVP (architecture document §43 v0.4) on top of the virtualization
/// kernel.
///
/// Design invariants:
///  - HeaderGeometry is the single source of truth for column geometry and
///    state; the body, widget headers and the business row widgets all query
///    it (§14, §45.10),
///  - rows are virtualized exactly like VirtualListView: only visible rows,
///    overscan rows and pinned rows own a QWidget,
///  - every materialized row is one business QWidget (Row Widget Mode) whose
///    columns are positioned by ColumnHost or by the adapter hook, so no
///    cell-level QWidget is created (§45.8).
///
/// Cell Widget Mode with 2D (row x column) virtualization arrives with v0.5.
class VIRTUALITEMVIEWS_EXPORT VirtualTableView : public VirtualItemView
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
    /// Takes ownership of \a header; nullptr selects the default LabelHeaderView.
    void setHorizontalHeader(HeaderViewInterface *header);
    HeaderViewInterface *horizontalHeader() const { return m_horizontalHeader; }
    void setVerticalHeader(HeaderViewInterface *header);
    HeaderViewInterface *verticalHeader() const { return m_verticalHeader; }
    void setHorizontalHeaderVisible(bool visible);
    bool isHorizontalHeaderVisible() const { return m_horizontalHeaderVisible; }
    void setVerticalHeaderVisible(bool visible);
    /// The request of the application. The strip is only on screen while the current
    /// row heights can be mirrored - see isVerticalHeaderShown().
    bool isVerticalHeaderVisible() const { return m_verticalHeaderVisible; }
    /// Whether the row-number strip is actually shown: the request above *and* the
    /// current model being mirrorable. A variable-height model above the mirror limit
    /// hides the strip (with one qWarning()) while the request stays untouched, so it
    /// comes back by itself when the model shrinks or the heights become uniform.
    bool isVerticalHeaderShown() const { return m_verticalHeaderVisible && m_verticalHeaderSupported; }
    void setHeaderHeight(int height);
    int headerHeight() const { return m_headerHeight; }
    void setVerticalHeaderWidth(int width);
    int verticalHeaderWidth() const { return m_verticalHeaderWidth; }

    // -- header drag gestures ------------------------------------------------
    /// Drag a column to a new position (the column header's reorder gesture). **Off by
    /// default**: the drag changes the column order, so an application opts in.
    ///
    /// The order it writes lives in HeaderGeometry, so the body follows either way; the
    /// renderer that gets the gesture is the installed header *and* every pane clone (a
    /// drag inside a frozen pane reorders like one in the scrolling pane). Column reordering
    /// never needs a model.
    void setColumnDragEnabled(bool enabled);
    bool isColumnDragEnabled() const { return m_columnDragEnabled; }

    /// Drag a row number to a new position (the row-number strip's reorder gesture). **Off
    /// by default**, and - unlike the column side - it needs a model that records the move:
    /// a committed drag is reported through rowMoveRequested() and then applied with
    /// `model()->moveRows()`, and the default `QAbstractItemModel::moveRows()` implementation
    /// simply refuses it (the preview snaps back, the order stays).
    ///
    /// **So a model of your own has to either derive from viv::ReorderableTableModel, which
    /// records the order and implements moveRows(), or implement moveRows() itself** (and
    /// read its data through the recorded order, see that class). Without one of the two,
    /// turning this on changes nothing but the preview.
    ///
    /// Turning it on while the view has **no** model instantiates a
    /// `ReorderableTableModel` (owned by the view, 0 rows x 0 columns until the application
    /// fills it) and installs it, so the gesture is usable without a model of your own.
    /// Setting a model - before or after - always wins: no further model is instantiated
    /// then, and the one that was installed stays alive as a child of the view.
    void setVerticalHeaderDragEnabled(bool enabled);
    bool isVerticalHeaderDragEnabled() const { return m_verticalHeaderDragEnabled; }

    /// Drag a section edge to resize it - the column width, or the row height when the
    /// switch is about the row-number strip. **On by default** (a header is expected to be
    /// resizable, like QHeaderView's Interactive mode); switching it off leaves the size
    /// APIs untouched and only removes the gesture (and the resize cursor).
    void setColumnResizeEnabled(bool enabled);
    bool isColumnResizeEnabled() const { return m_columnResizeEnabled; }
    void setVerticalHeaderResizeEnabled(bool enabled);
    bool isVerticalHeaderResizeEnabled() const { return m_verticalHeaderResizeEnabled; }

    // -- geometry ------------------------------------------------------------
    int columnCount() const;
    ColumnGeometry columnGeometry(int logicalIndex) const;
    int columnWidth(int logicalIndex) const;
    using ColumnSpacingFactory = std::function<QWidget *(int, QWidget *)>;
    using ColumnSpacingBinder = std::function<void(QWidget *, int)>;
    /// Blank pixels between visible columns, including pane boundaries.
    void setColumnSpacing(int pixels);
    int columnSpacing() const { return m_columns ? m_columns->sectionSpacing() : 0; }
    /// Optional content for visible column gaps; the binder refreshes reused widgets.
    void setColumnSpacingFactory(ColumnSpacingFactory factory, ColumnSpacingBinder binder = {});
    void setHeaderColumnSpacingFactory(ColumnSpacingFactory factory, ColumnSpacingBinder binder = {});
    void setVerticalSpacingLineThroughRowSpacing(bool enabled);
    bool verticalSpacingLineThroughRowSpacing() const { return m_verticalSpacingLineThroughRowSpacing; }
    void setHorizontalSpacingLineThroughColumnSpacing(bool enabled);
    bool horizontalSpacingLineThroughColumnSpacing() const { return m_horizontalSpacingLineThroughColumnSpacing; }
    /// Grid lines remain visible at zero spacing; each direction can be hidden independently.
    void setVerticalGridLinesVisible(bool visible);
    bool verticalGridLinesVisible() const { return m_verticalGridLinesVisible; }
    void setHorizontalGridLinesVisible(bool visible);
    bool horizontalGridLinesVisible() const { return m_horizontalGridLinesVisible; }
    /// Invalid color follows the current style; width is at least one pixel.
    void setVerticalGridLineColor(const QColor &color);
    QColor verticalGridLineColor() const { return m_verticalGridLineColor; }
    void setHorizontalGridLineColor(const QColor &color);
    QColor horizontalGridLineColor() const { return m_horizontalGridLineColor; }
    void setVerticalGridLineWidth(int pixels);
    int verticalGridLineWidth() const { return m_verticalGridLineWidth; }
    void setHorizontalGridLineWidth(int pixels);
    int horizontalGridLineWidth() const { return m_horizontalGridLineWidth; }
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
    /// Share of the leftover width this column takes: the columns with a factor > 0 split
    /// "viewport width - the widths the other visible columns keep" in proportion to their
    /// factors, so 1 : 2 : 1 makes the third column twice as wide as the first. A factor of
    /// 0 - the default - is a column that keeps its own width, and a column the user
    /// drags to a new width becomes fixed again (see HeaderGeometry::
    /// setSectionStretchFactor(), which is where the widths themselves live: the header
    /// and the body both read them, so they can never disagree).
    ///
    /// The columns fill the viewport, so a table whose columns all stretch has nothing to
    /// scroll horizontally. The factors are part of the saved header state; the viewport
    /// width a restored state is measured against is the one of the view restoring it.
    void setColumnStretchFactor(int logicalIndex, qreal factor);
    qreal columnStretchFactor(int logicalIndex) const;
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
    /// The row pane boundary lines reach across the row-number strip as well, so the
    /// horizontal and the vertical boundaries look the same (§31, docs/history/row-freezing.md).
    int itemPaneSeparatorLeftExtension() const override;
    /// Same colour the column boundary uses - the one the current style paints section
    /// separators with - so both directions look identical.
    QColor itemPaneSeparatorColor() const override;
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
    /// Colour the current style paints a header section separator with (probed by
    /// rendering a section and reading its edge pixel). Both pane boundary lines - the
    /// header edge and the body line - use it, so they match the separators between the
    /// other columns instead of a guessed palette role.
    ///
    /// This is pane composition, not a renderer's business (§17 of the vertical-header
    /// decision): it used to live on the native QHeaderView adapter.
    static QColor sectionSeparatorColor(const QWidget *context);
    /// Paints a pane boundary line into \a rect (its layout is defined by the style; solid
    /// lines fill the rect, dashed ones are centred on it).
    static void drawPaneSeparator(QPainter *painter, const QRect &rect,
                                  const PaneSeparatorStyle &style,
                                  const QColor &styleSeparatorColor);
    /// Columns the last horizontal layout pass looked at (diagnostics/tests): the
    /// structural pass is O(total columns), a pure scroll is O(panes x log columns).
    qsizetype horizontalLayoutColumnVisits() const { return m_panes.columnVisitsInLastUpdate(); }

    // -- header animation (§23/§24) ------------------------------------------
    /// Visual geometry animation of the installed header renderers: a section move
    /// slides the section to its committed position instead of teleporting, while
    /// the body keeps reading the committed HeaderGeometry (it jumps once, at the
    /// commit). Resizing, scrolling and pane changes stay immediate, because there
    /// the body follows every frame. Renderers without their own section placement
    /// (a native QHeaderView) ignore the setting. Default: enabled, 300 ms, OutCubic.
    void setHeaderAnimationEnabled(bool enabled);
    bool headerAnimationEnabled() const { return m_headerAnimationEnabled; }
    /// Duration of the visual transition in milliseconds; 0 turns it off.
    void setHeaderAnimationDuration(int ms);
    int headerAnimationDuration() const { return m_headerAnimationDuration; }
    /// Whether the body's column widgets follow the header's *visual* section
    /// positions (§23/§24) - the dragged column and the columns making room for it
    /// during a header drag, and the sliding column of a committed move - instead of
    /// waiting for the commit and jumping there.
    ///
    /// Only widget headers that can report a visual position take part
    /// (VirtualHeaderView, see HeaderViewInterface::sectionVisualX()); the native
    /// adapter paints its sections itself, so there the header and the body change
    /// together at the commit whatever this is set to. The committed geometry stays
    /// authoritative throughout: columnGeometry(), hit testing, spans, the scroll
    /// bar and every query keep reading it, and only x is taken from the header -
    /// the body never drives a header animation back into the geometry.
    ///
    /// Default: enabled, which is what makes a header drag read as one movement.
    void setColumnFollowsHeaderVisual(bool follows);
    bool columnFollowsHeaderVisual() const { return m_columnFollowsHeaderVisual; }
    /// The row analogue of the setting above: while the row-number strip is dragged, the
    /// body's rows follow the *visual* position of their section (the dragged row follows
    /// the pointer, the rows making room slide along), instead of waiting for the commit.
    ///
    /// Only the rows of the band being dragged follow; a frozen row pane keeps its rows
    /// pinned (they are pinned at the edge by definition, and a drag never crosses a band).
    /// The committed layout stays authoritative for everything else - `visualRect()`,
    /// hit testing, the scroll bars and the model order never see an intermediate frame -
    /// and only the framework-managed row widgets move. Default: enabled.
    void setRowFollowsHeaderVisual(bool follows);
    bool rowFollowsHeaderVisual() const { return m_rowFollowsHeaderVisual; }

    // -- row heights ---------------------------------------------------------
    void setRowSizePolicy(RowSizePolicy policy);
    RowSizePolicy rowSizePolicy() const { return m_rowSizePolicy; }
    int rowHeight(qsizetype row) const;
    /// Explicit (user) row height; wins over measurement by default.
    void setRowHeight(qsizetype row, int height);
    void clearRowHeight(qsizetype row);
    bool hasExplicitRowHeight(qsizetype row) const;

    // -- spans (§43 "spans", see docs/history/spans.md) ------------------------------
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
    /// Restores the column state, the frozen pane sets and the frozen row counts.
    /// Parsing and validation happen before anything is applied, so a `false`
    /// return means the view was not touched at all.
    bool restoreHeaderState(const QByteArray &state);

    // -- adapter -------------------------------------------------------------
    /// Polymorphic override of the adapter entry point. The table needs both pointers to name
    /// the same object: the recycler factory, `createItem()` and every row layout read the
    /// *table* adapter, while the base kernel reads `m_adapter`. Letting them differ is not a
    /// "does nothing" bug - adapter B then binds the QWidgets adapter A created, which is UB
    /// as soon as their `WidgetType` namespaces disagree (P1 of the fourth review).
    ///
    /// A plain `WidgetAdapter` is therefore rejected with a warning (the installed adapter
    /// stays), and `nullptr` clears the adapter like the base setter does. `takeOwnership`
    /// is only taken for an adapter the call accepts.
    void setAdapter(WidgetAdapter *adapter, bool takeOwnership = false) override;
    /// Typed convenience entry point: same as setAdapter() for a caller that already holds a
    /// TableWidgetAdapter (kept because the examples and the docs use this name).
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
    /// Whether hover and selection backgrounds are queried per row or per cell.
    /// This is independent of RowWidgets/CellWidgets materialization.
    enum class VisualStateScope { Row, Cell };
    Q_ENUM(VisualStateScope)
    void setVisualStateScope(VisualStateScope scope);
    VisualStateScope visualStateScope() const { return m_visualStateScope; }
    VisualState visualState(const QModelIndex &index) const override;
    QModelIndex cellIndexForWidget(const QWidget *widget) const;
    QList<QModelIndex> materializedCellIndexes() const;

signals:
    void sortIndicatorRequested(int logicalIndex, Qt::SortOrder order);
    /// A drag on the row-number strip wants the rows at \a fromRow..\a fromRow moved to
    /// \a toRow (both are view rows). The view then asks the model
    /// (`moveRows()`); a model that cannot move rows leaves the order alone, and an
    /// application that implements the move itself can ignore this signal.
    void rowMoveRequested(int fromRow, int toRow);
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
    void refreshVisualStates() override;
    void refreshVisualState(const QModelIndex &index) override;
    QModelIndex indexAt(const QPoint &viewportPos) const override;
    bool canMeasureItem(qsizetype item) const override;
    void afterMaterialize() override;
    void applyRowSpacingOverrides() override;
    void configureRowSpacingWidget(QWidget *widget) const override;
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
    /// Deletes a header renderer through its interface (a renderer does not have
    /// to be a QWidget itself, so deleting `headerWidget()` would leak the
    /// wrapper) and clears the pointer.
    void deleteHeader(HeaderViewInterface *&header);
    /// Re-binds every materialized row after a column structure change, so a business row
    /// widget that builds its column schema in bindWidget() can rebuild it (Row Widget Mode).
    void rebindMaterializedRows();
    /// Drops / rebuilds the derived pane renderers of the horizontal header when its adapter
    /// is replaced (they borrow that adapter, see VirtualHeaderView::adapterAboutToChange()).
    void dropDerivedPaneHeaders();
    void rebuildDerivedPaneHeaders();
    /// Frozen row strips borrow the primary vertical header's adapter.
    void dropFrozenRowHeaders();
    void rebuildFrozenRowHeaders();
    /// Cell Widget Mode: a cell has to be unbound *before* the model invalidates
    /// its persistent index, otherwise the business loses the row / column the
    /// widget was bound to.
    void onRowsAboutToBeRemovedForCells(const QModelIndex &parent, int first, int last);
    void onColumnsAboutToBeRemovedForCells(const QModelIndex &parent, int first, int last);
    void onModelAboutToBeResetForCells();
    void recycleCellsInRowRange(const QModelIndex &parent, int first, int last);
    void recycleCellsInColumnRange(const QModelIndex &parent, int first, int last);

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
    /// Keeps the row-number strip aligned with the body while scrolling.
    void updateRowHeaderOffset();
    void syncHorizontalScrollBar();
    void updateColumnLayout();
    /// Recomputes the pane layout (§31) and the column layout that depends on
    /// it (frozen widths change the scrollable range and every column x).
    void updatePaneLayout();
    /// Scroll-only refresh: recompute the pane windows from their cached prefix
    /// sums (O(panes x log columns)) instead of the whole pane layout.
    void updatePaneLayoutForScroll();
    /// Creates/destroys/updates the frozen pane header renderers.
    void syncHeaderPanes();
    /// Creates/positions the 1 px body lines of the pane boundaries.
    void syncPaneSeparatorLines();
    /// Pane header of the same kind as the installed horizontal header.
    HeaderViewInterface *createHorizontalPaneHeader();
    /// Renderer the view installs for the horizontal header when the application
    /// does not install one: a LabelHeaderView (a widget header with the built-in
    /// label-only adapter). See the class comment of LabelHeaderView.
    HeaderViewInterface *createDefaultHorizontalHeader();
    /// Same for the row-number strip: a LabelHeaderView(Qt::Vertical) - the same widget
    /// renderer as the column header, other axis, so it virtualizes (a ten-million-row
    /// uniform table materializes only its window) and supports the same gestures.
    HeaderViewInterface *createDefaultVerticalHeader();
    /// Lets \a strip report a row move instead of reordering its geometry, and wires
    /// that request to rowMoveRequested() + moveRows().
    void watchRowStrip(HeaderViewInterface *strip);
    /// Row move of a strip drag: report it, then ask the model to move the rows.
    void moveRowsForStripDrag(int fromRow, int toRow);
    /// Forwards the gesture switches (drag / resize, both axes) to the installed renderers
    /// - the installed headers, the pane clones and the frozen-row bands. A renderer that
    /// is not a VirtualHeaderView (a business' own HeaderViewInterface) has no such
    /// switches and keeps its own behaviour.
    void applyHeaderGestureSettings();
    /// Row-number strip of a frozen row pane (§31 row direction): same kind as the
    /// installed vertical header. Null when the installed one cannot be cloned (a custom
    /// renderer that is not a VirtualHeaderView), in which case the strip stays single.
    HeaderViewInterface *createVerticalPaneHeader();
    /// Creates/destroys/positions the row-number strips of the frozen row panes: each
    /// band of the strip shows its own rows with its own offset, because the content
    /// positions (and therefore the row heights) are shared with the body.
    void syncVerticalPaneHeaders();
    /// Places every row-number strip on its pane rectangle (the whole viewport when no
    /// row is frozen).
    void layoutVerticalHeaderStrips();
    /// Pushes the animation settings (§23/§24) into every header renderer.
    void applyHeaderAnimationSettings();
    /// Asks every header renderer for a visual transition (or clears the request)
    /// around a programmatic section move (§23).
    void requestSectionMoveAnimation(bool animated);
    /// Lets \a header report its visual placements (see setVisualGeometryCallback()).
    /// Called for the installed header and for every derived pane renderer.
    void watchHeaderVisualGeometry(HeaderViewInterface *header);
    /// Visual x of \a logicalIndex while a horizontal renderer draws it away from
    /// its committed position; false when every renderer is on the committed
    /// geometry (or has no visual state for that section).
    bool columnVisualX(int logicalIndex, int *viewportX) const;
    /// The renderer placed its sections: move the body's columns in the same frame.
    void onHeaderVisualGeometryFrame();
    /// Geometry-only refresh while the body follows a header animation: re-positions
    /// the framework-managed column hosts / cells, without running an adapter hook,
    /// creating or recycling widgets (only x changes).
    void updateVisualColumnGeometry();
    /// Row-number strip that is currently drawing a visual (preview) geometry, or null.
    /// The frozen band strips are deliberately not asked: their rows stay pinned.
    VirtualHeaderView *visualRowStrip() const;
    /// Puts every materialized row widget on its visual y - the committed one while no
    /// strip is dragging, which is also how the rows come back after a commit/cancel.
    void updateVisualRowGeometry();
    /// Lifts the body lines above the (re)materialized items.
    void raisePaneSeparatorLines();
    void syncColumnSpacingWidgets();
    void syncRowGridLines();
    void applyGridLineVisibilityToHeaders();
    QColor resolvedVerticalGridLineColor() const;
    QColor resolvedHorizontalGridLineColor() const;
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
    /// Places the framework-managed column hosts of \a item and then hands the row
    /// to the adapter hook. \a notifyAdapter is false for the geometry-only refresh
    /// that follows a header animation frame (see updateVisualColumnGeometry()).
    void applyColumnLayout(const MaterializedItem &item, bool notifyAdapter = true);
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
    /// Re-applies the heights the user set (m_explicitRowHeights, keyed by row identity) after a
    /// layout change rebuilt every derived size from the estimate.
    void reapplyExplicitRowHeights();
    /// Cell granularity (SelectionBehavior::SelectItems) previews only the dragged cell: the
    /// materialized widget is the whole row, so the host the framework placed for that column is
    /// what the pixmap is cut down to.
    QRect dragPixmapRect(const QModelIndex &index) const override;
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
    VisualStateScope m_visualStateScope = VisualStateScope::Row;
    HeaderGeometry *m_columns = nullptr;
    /// Compresses the 64-bit horizontal offset into the int-only scroll bar (the
    /// vertical axis has the same mapper in the kernel).
    ScrollMapper m_horizontalMapper;
    TablePaneLayout m_panes;
    HeaderGeometry *m_rowHeaders = nullptr;
    HeaderViewInterface *m_horizontalHeader = nullptr;
    HeaderViewInterface *m_verticalHeader = nullptr;
    /// Row-number strips of the frozen row panes (top / bottom band); the installed
    /// vertical header keeps the scrolling band (§31 row direction).
    HeaderViewInterface *m_frozenTopRowsHeader = nullptr;
    HeaderViewInterface *m_frozenBottomRowsHeader = nullptr;
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
    QHash<int, QWidget *> m_columnSpacingWidgets;
    QVector<QWidget *> m_columnSpacingPool;
    QHash<qsizetype, QWidget *> m_rowGridLines;
    QVector<QWidget *> m_rowGridLinePool;
    ColumnSpacingFactory m_columnSpacingFactory;
    ColumnSpacingBinder m_columnSpacingBinder;
    ColumnSpacingFactory m_headerColumnSpacingFactory;
    ColumnSpacingBinder m_headerColumnSpacingBinder;
    bool m_verticalSpacingLineThroughRowSpacing = true;
    bool m_horizontalSpacingLineThroughColumnSpacing = true;
    bool m_verticalGridLinesVisible = true;
    bool m_horizontalGridLinesVisible = true;
    QColor m_verticalGridLineColor;
    QColor m_horizontalGridLineColor;
    int m_verticalGridLineWidth = 1;
    int m_horizontalGridLineWidth = 1;
    PaneSeparatorStyle m_paneSeparatorStyle;
    bool m_headerAnimationEnabled = true;
    int m_headerAnimationDuration = 300;
    /// Body follows the header's visual section geometry (§23/§24).
    bool m_columnFollowsHeaderVisual = true;
    /// Rows follow the row-number strip's visual section geometry (§8).
    bool m_rowFollowsHeaderVisual = true;
    /// True while the rows carry a preview offset (see updateVisualRowGeometry()).
    bool m_rowVisualOffsetsActive = false;
    /// Re-entrancy guard: a visual frame must not start another one.
    bool m_visualGeometryFrameActive = false;
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
    RowSizePolicy m_rowSizePolicy = RowSizePolicy::ExplicitWins;
    bool m_sortingEnabled = false;
    bool m_sortGuard = false;
    QHash<QPersistentModelIndex, int> m_explicitRowHeights;
    /// Effective support of the row-number strip: false while the model needs per-row
    /// heights above the mirror limit. Kept apart from m_verticalHeaderVisible (the
    /// request), so the strip comes back when the reason is gone (P2-9).
    bool m_verticalHeaderSupported = true;
    bool m_columnUpdateActive = false;
    /// A stretch pass can re-enter updatePaneLayout() through the geometry signals: the
    /// outer call keeps laying the panes out, the inner one is a no-op.
    bool m_paneLayoutActive = false;
    bool m_rowHeaderUpdateActive = false;
    bool m_headersLaidOut = false;
    /// Opt-in drag gestures (see setColumnDragEnabled() / setVerticalHeaderDragEnabled()).
    bool m_columnDragEnabled = false;
    bool m_verticalHeaderDragEnabled = false;
    /// Resize gestures, on unless switched off (see setColumnResizeEnabled()).
    bool m_columnResizeEnabled = true;
    bool m_verticalHeaderResizeEnabled = true;
    /// Set once the application installed a model of its own: from then on the view never
    /// instantiates the row-drag model, whatever the switch does.
    bool m_applicationModelSeen = false;
    /// The ReorderableTableModel the view instantiated for the row drag (null while the
    /// application provided a model). Owned through its QObject parent.
    QPointer<QAbstractItemModel> m_internalModel;
};

} // namespace viv
