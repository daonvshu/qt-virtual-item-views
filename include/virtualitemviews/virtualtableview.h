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
#include <memory>

class QAbstractItemModel;
class QPainter;
class QKeyEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

namespace viv {

class VirtualHeaderView;
class RowDragPreview;

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

    /// Drop locations for SelectItems; SelectRows always inserts between rows.
    /// VirtualTreeTableView retains its hierarchical node drop locations.
    enum class DropTargetMode {
        BetweenRows,
        OnCell,
        BetweenRowsOrOnCell,
    };
    Q_ENUM(DropTargetMode)

    /// Default BetweenRows preserves insertion-only drops. In combined mode,
    /// the top/bottom quarter inserts and the middle targets the existing cell.
    /// OnCell calls the model with parent = cell and row = column = -1;
    /// the model decides whether to fill, replace or reject its data.
    void setDropTargetMode(DropTargetMode mode);
    DropTargetMode dropTargetMode() const { return m_dropTargetMode; }

    /// Native body-drag presentation, independent of model/drop semantics.
    enum class DragFeedbackMode { Indicator, RowMoveAnimation };
    Q_ENUM(DragFeedbackMode)
    /// Default RowMoveAnimation. Previews an internal single-row Move
    /// in SelectRows, in the scrolling row pane, without a span provider. Other
    /// drops use the indicator. Model order changes only when the model accepts.
    void setDragFeedbackMode(DragFeedbackMode mode);
    DragFeedbackMode dragFeedbackMode() const { return m_dragFeedbackMode; }
    /// Row-reorder tween duration in milliseconds (default 180); zero is immediate.
    void setDragReorderAnimationDuration(int milliseconds);
    int dragReorderAnimationDuration() const { return m_dragReorderAnimationDuration; }
    /// Curve for rows making room (default OutCubic); separate from header animation.
    void setDragReorderAnimationEasingCurve(const QEasingCurve &curve);
    QEasingCurve dragReorderAnimationEasingCurve() const { return m_dragReorderEasingCurve; }

    explicit VirtualTableView(QWidget *parent = nullptr);
    ~VirtualTableView() override;

    void setModel(QAbstractItemModel *model) override;

    // -- headers -------------------------------------------------------------
    HeaderGeometry *horizontalHeaderGeometry() const { return m_columns; }
    HeaderGeometry *verticalHeaderGeometry() const { return m_rowHeaders; }
    /// Takes ownership of \a header; nullptr selects the default VirtualHeaderView.
    void setHorizontalHeader(HeaderViewInterface *header);
    HeaderViewInterface *horizontalHeader() const { return m_horizontalHeader; }
    void setVerticalHeader(HeaderViewInterface *header);
    HeaderViewInterface *verticalHeader() const { return m_verticalHeader; }
    /// Takes ownership of the widget at the top-left header intersection.
    /// Replacing it hides and schedules deletion of the previous widget.
    /// nullptr restores the default widget, styled like the built-in headers.
    /// The view sizes it to the row-header width and column-header height,
    /// and shows it only while both header strips are shown.
    void setHeaderCornerWidget(QWidget *widget);
    QWidget *headerCornerWidget() const { return m_headerCornerWidget.data(); }
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

    /// Shows built-in header base backgrounds, including pane clones and the default corner.
    /// On by default. False exposes the parent background while retaining state layers,
    /// text and separators. Convenience setter for both installed headers.
    /// The getter reports the horizontal header; configure each header directly for axis styles.
    void setHeaderBackgroundVisible(bool visible);
    bool headerBackgroundVisible() const;
    /// Flat base color for built-in headers, pane clones and the default corner.
    /// Invalid restores native panels. Background visibility takes precedence;
    /// hover/selection layers and explicit gap colors remain independent.
    /// Convenience setter for both axes; the getter reports the horizontal header.
    void setHeaderBackgroundColor(const QColor &color);
    QColor headerBackgroundColor() const;

    /// Click a column header to select its complete column. Off by default.
    /// While enabled, a plain header click selects instead of requesting a sort.
    /// Body selection gestures and the visual state scope are unchanged.
    void setHorizontalHeaderSelectionEnabled(bool enabled);
    bool isHorizontalHeaderSelectionEnabled() const { return m_horizontalHeaderSelectionEnabled; }
    /// Click a row header to select its complete row. On by default.
    /// Both axes respect SelectionMode, Ctrl/Shift and model item flags.
    void setVerticalHeaderSelectionEnabled(bool enabled);
    bool isVerticalHeaderSelectionEnabled() const { return m_verticalHeaderSelectionEnabled; }
    /// Shows built-in header hover/selection backgrounds for one axis. On by default.
    /// Independent of body backgrounds, header click selection and state queries.
    void setHeaderVisualStateBackgroundVisible(Qt::Orientation orientation, bool visible);
    bool headerVisualStateBackgroundVisible(Qt::Orientation orientation) const;
    /// Row headers require a complete row; column headers require a complete column.
    /// Row headers also follow row-scope hover, including its animation.
    /// Both axes fade selection with the view's visual state duration and easing curve.
    /// Row section numbers refer to view rows, including flattened tree rows.
    VisualState headerVisualState(Qt::Orientation orientation, int logicalIndex) const;

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
    /// switch is about the row-number strip. Both gestures are off by default;
    /// enabling one adds its resize handle and cursor. The size APIs are unaffected.
    void setColumnResizeEnabled(bool enabled);
    bool isColumnResizeEnabled() const { return m_columnResizeEnabled; }
    void setVerticalHeaderResizeEnabled(bool enabled);
    bool isVerticalHeaderResizeEnabled() const { return m_verticalHeaderResizeEnabled; }

    // -- geometry ------------------------------------------------------------
    virtual int columnCount() const;
    ColumnGeometry columnGeometry(int logicalIndex) const;
    int columnWidth(int logicalIndex) const;
    using ColumnSpacingFactory = std::function<QWidget *(int, QWidget *)>;
    using ColumnSpacingBinder = std::function<void(QWidget *, int)>;
    /// Blank pixels between visible columns, including pane boundaries.
    void setColumnSpacing(int pixels);
    int columnSpacing() const { return m_columns ? m_columns->sectionSpacing() : 0; }
    /// Background of body row and column gaps; invalid restores palette defaults.
    void setSpacingBackgroundColor(const QColor &color) override;
    /// Extends whole-row hover/selection backgrounds across column gaps. Default true.
    /// SelectItems stays cell-local; row gaps and header gaps are never covered.
    void setVisualStateThroughColumnSpacing(bool enabled);
    bool visualStateThroughColumnSpacing() const { return m_visualStateThroughColumnSpacing; }
    /// Background of gaps in both widget headers, including frozen panes.
    /// Invalid follows the header base background setting; custom renderers own their painting.
    /// Convenience setter for both axes; the getter reports the horizontal header.
    void setHeaderSpacingBackgroundColor(const QColor &color);
    QColor headerSpacingBackgroundColor() const;
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
    /// FitViewport keeps visible column widths plus gaps equal to the viewport width.
    /// Edge drags compensate on that side using press-time width proportions. Extreme
    /// viewport sizes relax width limits as needed. The default is Independent.
    enum class ColumnResizeMode { Independent, FitViewport };
    Q_ENUM(ColumnResizeMode)
    void setColumnResizeMode(ColumnResizeMode mode);
    ColumnResizeMode columnResizeMode() const;
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
    /// 0 - the default - is a column that keeps its own width. Dragging any column edge
    /// or calling setColumnWidth() clears all factors and disables last-
    /// column stretching. Other columns keep their current widths until the application
    /// sets positive factors again (see HeaderGeometry::setSectionStretchFactor()).
    ///
    /// The columns fill the viewport, so a table whose columns all stretch has nothing to
    /// scroll horizontally. The factors are part of the saved header state; the viewport
    /// width a restored state is measured against is the one of the view restoring it.
    void setColumnStretchFactor(int logicalIndex, qreal factor);
    qreal columnStretchFactor(int logicalIndex) const;
    void setStretchLastColumn(bool stretch);
    bool stretchLastColumn() const;

    // -- horizontal scrolling -------------------------------------------------
    /// Offset of the primary scroll group: the group the header
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
    /// horizontal scroll bar drive, regardless of its group number. Every other
    /// group is driven by setHorizontalOffset(group, offset).
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
    /// Sets the offset of \a scrollGroup. For the primary group this delegates to
    /// setHorizontalOffset(qint64), keeping the header and scroll bar in sync.
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
    /// the current provider; an owned provider is released after active queries return.
    void setSpanProvider(TableSpanProvider *provider, bool takeOwnership = false);
    TableSpanProvider *spanProvider() const { return m_spanProvider; }
    /// Convenience for the map provider: merges the cells starting at the
    /// visible (\a row, \a column). Creates the owned TableSpanMap on first use.
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
    /// Paints complete hover/selection backgrounds below transparent row/cell widgets.
    /// Enabled by default; row gaps stay clear and column gaps follow visualStateThroughColumnSpacing().
    /// Hover and selection follow SelectionBehavior, independently of materialization.
    /// Colors and animation use VirtualItemView's visual-state settings.
    virtual void setVisualStateBackgroundVisible(bool visible);
    bool visualStateBackgroundVisible() const { return m_visualStateBackgroundVisible; }
    /// Shared hover/selection radius in logical pixels; negative values become zero.
    /// Default zero. Applies to row or cell backgrounds according to SelectionBehavior.
    /// Corners follow the complete row or cell bounds, including offscreen ends.
    void setVisualStateCornerRadius(int radius);
    int visualStateCornerRadius() const { return m_visualStateCornerRadius; }
    /// Extends the state radius to built-in headers. Enabled by default.
    /// SelectRows rounds only the row header's left corners; row content joins with square left corners.
    /// SelectItems rounds all four corners of row headers and cells independently.
    /// Column headers round all four corners. Hidden header backgrounds leave body corners intact.
    void setVisualStateCornerRadiusIncludesHeaders(bool enabled);
    bool visualStateCornerRadiusIncludesHeaders() const { return m_visualStateCornerRadiusIncludesHeaders; }
    VisualState visualState(const QModelIndex &index) const override;
    QModelIndex indexForWidget(const QWidget *widget) const override;
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
    bool m_visualStateBackgroundVisible = true;
    void paintEvent(QPaintEvent *event) override;
    /// Paints visible item state layers, always excluding row gaps.
    /// Column gap hosts call with paintColumnSpacing=true; the viewport excludes their bands.
    virtual void paintVisualStateBackgrounds(QPainter *painter, const QRect &dirtyRect,
                                            bool paintColumnSpacing = false) const;
    /// Paints one state layer using the configured visibility and corner radius.
    /// Override for custom shapes; the clip includes column gaps only when configured.
    /// Adapter widgets must leave their background transparent.
    virtual void paintStateBackgroundLayer(QPainter *painter, const QRect &extended,
                                          const QRegion &clip,
                                          const QColor &color) const;
    /// Draws a rounded layer with shape and clip edges aligned to physical pixels.
    void paintRoundedStateBackgroundLayer(QPainter *painter, const QRect &extended,
                                          const QRegion &clip, const QColor &color,
                                          int radius) const;
    /// Whether a row state layer joins a visible row-header background at its left edge.
    bool rowVisualStateIncludesHeader() const;
    /// Parent whose columns define the shared table schema.
    virtual QModelIndex columnSchemaParent() const;
    virtual bool isColumnSchemaParent(const QModelIndex &parent) const;
    /// The default implementation moves flat top-level rows.
    virtual void moveRowsForStripDrag(int fromRow, int toRow);
    /// A tree can reject a span that crosses a visible hierarchy boundary.
    virtual bool isSpanValid(const QModelIndex &anchor, const TableSpan &span) const;
    virtual QRect spanRowRect(qsizetype row) const;
    /// Space reserved before the content of a cell (zero for a flat table).
    virtual int leadingCellInset(const QModelIndex &index) const;
    /// Portion of a horizontal row line occupied by tree decoration, in viewport x.
    virtual QRect rowGridLineExclusion(int depth) const;
    /// Visual x of a column while its header section is being dragged or animated.
    bool columnVisualX(int logicalIndex, int *viewportX) const;
    /// Reports a scrolling row's preview y only while row following is enabled.
    bool rowVisualY(qsizetype row, int *viewportY) const;
    /// Refreshes the horizontal line masks after tree decoration moves.
    void syncRowGridLines();
    /// Lets derived views customize the background of column spacing hosts.
    virtual void configureColumnSpacingWidget(QWidget *widget) const;
    /// Lets derived views move decorations with a header's visual column position.
    virtual void visualColumnGeometryChanged() {}
    /// Reapply row heights keyed by persistent model indexes after visible rows change.
    void reapplyExplicitRowHeights();
    /// Reapply heights only for newly visible rows, preserving measurements elsewhere.
    void reapplyExplicitRowHeightsInRange(qsizetype first, qsizetype last);
    void rekeyPersistentTableState();
    void recycleAllCells();
    qsizetype viewItemCount() const override;
    QModelIndex viewIndex(qsizetype item, int column = 0) const override;
    qsizetype viewItemForIndex(const QModelIndex &index) const override;
    bool isLayoutParent(const QModelIndex &parent) const override;
    QModelIndex indexForNavigation(qsizetype item, const QModelIndex &current) const override;
    void materializeItems(const VisibleRange &rows) override;
    void augmentMaterializationRanges(QVector<VisibleRange> &ranges) const override;
    /// Cell Widget Mode materializes every range the kernel asks for: the scrolling
    /// window plus the frozen rows (§31 row direction).
    void materializeItemRanges(const QVector<VisibleRange> &ranges) override;
    void rebindItemsInRange(const QModelIndex &topLeft, const QModelIndex &bottomRight) override;
    void refreshVisualStates() override;
    void refreshVisualState(const QModelIndex &index) override;
    bool isHoverTrackingLocked() const override;
    QModelIndex hoverIndexAtGlobalPosition(const QPoint &position) const override;
    QModelIndex indexAt(const QPoint &viewportPos) const override;
    bool canMeasureItem(qsizetype item) const override;
    void afterMaterialize() override;
    /// Rebinds every row strip after visible rows change identity.
    void refreshRowHeaderLabels();
    void applyRowSpacingOverrides() override;
    void configureRowSpacingWidget(QWidget *widget) const override;
    QRegion rowSpacingWidgetMask(qsizetype row, const QRect &geometry) const override;
    bool handleItemKeyPress(QKeyEvent *event) override;
    /// §38: a table drops between rows (row semantics) or into a cell. The
    /// column of the target is -1 when the whole row is the drop unit.
    VirtualItemView::DropTarget resolveDropTarget(const QPoint &viewportPos) const override;
    QRect resolveDropIndicatorRect(const DropTarget &target) const override;
    /// Row selection captures the whole row; cell selection captures the dragged cell.
    /// In cell-widget mode a row preview uses the visible viewport band.
    QRect dragPixmapRect(const QModelIndex &index) const override;
    /// Cell-widget mode uses the viewport for row selection and the anchor widget for cells.
    /// Row-widget mode uses the materialized row, cropped by dragPixmapRect() for cell selection.
    QWidget *dragSourceWidget(const QModelIndex &index) const override;
    /// Captures the composed table, including grid and pane separators, clipped to
    /// the source widget's viewport, pane and masks.
    QPixmap grabDragPreview(QWidget *sourceWidget, const QRect &capture) override;
    bool updateDragFeedback(const DropTarget &target, const QPoint &position, Qt::DropAction action,
                            const QList<QPersistentModelIndex> &sources) override;
    void clearDragFeedback() override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent *event) override;
    QModelIndex bindingDataIndex(const QModelIndex &index) const override;
    int bindingColumn(const QModelIndex &index) const override;
    void refreshItemData() override;

private:
    /// Visible viewport band of a row preview, clipped to its frozen or scrolling pane.
    QRect rowDragPreviewRect(const QModelIndex &index) const;
    /// Viewport bands occupied by actual columns, clipped to their owning panes.
    QRegion columnContentRegion() const;
    /// Current elapsed-time sample, so a fast retarget does not start from a stale tick.
    qreal dragReorderProgress() const;
    /// Repositions widgets, row gaps, state backgrounds and grid lines in one frame.
    void applyDragReorderFrame();
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
    /// Includes inserted cells only in rows whose existing columns are fully selected.
    void extendRowSelectionForInsertedColumns(const QModelIndex &parent, int first, int last);
    void onColumnsRemoved(const QModelIndex &parent, int first, int last);
    void onColumnsMoved(const QModelIndex &parent, int start, int end,
                        const QModelIndex &destinationParent, int destinationColumn);
    void onHeaderGeometryChanged();
    void onVerticalSectionResized(int logicalIndex, int oldSize, int newSize);
    /// Keeps the row-number strip aligned with the body while scrolling.
    void updateRowHeaderOffset();
    void syncHorizontalScrollBar();
    void updateColumnLayout();
    /// Visible merged rectangles projected onto the row strip's preview positions.
    QVector<QRect> previewSpanRects() const;
    /// Shares preview span projection only within one grid mask refresh.
    void refreshPreviewGridMasks();
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
    /// does not install one: a VirtualHeaderView with its built-in LabelHeaderAdapter.
    HeaderViewInterface *createDefaultHorizontalHeader();
    /// Same for the row-number strip: a VirtualHeaderView(Qt::Vertical) - the same widget
    /// renderer as the column header, other axis, so it virtualizes (a ten-million-row
    /// uniform table materializes only its window) and supports the same gestures.
    HeaderViewInterface *createDefaultVerticalHeader();
    /// Lets \a strip report a row move instead of reordering its geometry, and wires
    /// that request to rowMoveRequested() + moveRows().
    void watchRowStrip(HeaderViewInterface *strip);
    /// Row move of a strip drag: report it, then ask the model to move the rows.
    /// Forwards the gesture switches (drag / resize, both axes) to the installed renderers
    /// - the installed headers, the pane clones and the frozen-row bands. A renderer that
    /// is not a VirtualHeaderView (a business' own HeaderViewInterface) has no such
    /// switches and keeps its own behaviour.
    void applyHeaderGestureSettings();
    /// Connects click selection and pointer refresh for an installed or cloned header.
    void watchHeaderSelection(HeaderViewInterface *header);
    /// Refreshes the default corner and column-gap hosts after a header appearance change.
    void refreshHeaderAppearance();
    /// Handles a completed click without interfering with resize or reorder gestures.
    void onHeaderSectionClicked(int logicalIndex, Qt::KeyboardModifiers modifiers);
    /// Builds model ranges for row/column headers using the current visual mapping.
    QItemSelection headerSelectionRange(Qt::Orientation orientation,
                                       qsizetype first, qsizetype last) const;
    /// Repaints materialized sections after hover, selection or color changes.
    void refreshHeaderVisualStates();
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
    /// The renderer placed its sections: move the body's columns in the same frame.
    void onHeaderVisualGeometryFrame();
    /// Geometry-only refresh while the body follows a header animation: re-positions
    /// the framework-managed column hosts / cells, without running an adapter hook,
    /// creating or recycling widgets (only x changes).
    void updateVisualColumnGeometry();
    /// Active preview strip for the pane containing \a row, or null.
    /// A negative row queries any active row strip, including frozen bands.
    VirtualHeaderView *visualRowStrip(qsizetype row = -1) const;
    /// Row lifted by a header reorder preview, or -1 when the body is not following one.
    qsizetype headerDragRow() const;
    /// Puts every materialized row widget on its visual y - the committed one while no
    /// strip is dragging, which is also how the rows come back after a commit/cancel.
    void updateVisualRowGeometry();
    /// Lifts the body lines above the (re)materialized items.
    void raisePaneSeparatorLines();
    void syncColumnSpacingWidgets();
    /// Updates existing column-gap hosts for moving row gaps and horizontal lines.
    void updateColumnSpacingVisualRows();
    /// Visual row bounds for grid lines; the hidden drag source supplies the
    /// insertion placeholder's trailing boundary instead of leaving it open.
    QRect rowGridGeometry(qsizetype row) const;
    void applyGridLineVisibilityToHeaders();
    QColor resolvedVerticalGridLineColor() const;
    QColor resolvedHorizontalGridLineColor() const;
    QColor resolvedHeaderSpacingBackgroundColor() const;
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
    /// Pins the span anchor in cell mode, or the canonical row in row mode.
    QModelIndex dragSourcePinIndex(const QModelIndex &index) const override;
    void scrollToColumn(int logicalIndex);
    void onSortIndicatorChanged(int logicalIndex, Qt::SortOrder order);

    QVector<int> columnsForCellMaterialization() const;
    QRect cellRect(qsizetype row, int logicalColumn) const;
    QWidget *createCellWidget(const QPersistentModelIndex &index);
    void recycleCell(const QPersistentModelIndex &index, QWidget *widget);
    void discardCellWidget(QWidget *widget);
    /// Dispatches cell state callbacks from an index snapshot; an invalid row means all cells.
    /// With exactCell, only the supplied cell is notified.
    void notifyCellVisualStates(const QModelIndex &row, bool exactCell = false);
    bool isCellPinned(const QPersistentModelIndex &index, const QWidget *widget) const;
    void updateCellGeometry();

    ListLayout *m_rowLayout = nullptr;
    DropTargetMode m_dropTargetMode = DropTargetMode::BetweenRows;
    int m_visualStateCornerRadius = 0;
    bool m_visualStateCornerRadiusIncludesHeaders = true;
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
    QPointer<QWidget> m_headerCornerWidget;
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
    /// Leading edge of a lifted row, matching the row-number strip's drag border.
    QWidget *m_headerDragLeadingSeparator = nullptr;
    ColumnSpacingFactory m_columnSpacingFactory;
    ColumnSpacingBinder m_columnSpacingBinder;
    ColumnSpacingFactory m_headerColumnSpacingFactory;
    ColumnSpacingBinder m_headerColumnSpacingBinder;
    bool m_verticalSpacingLineThroughRowSpacing = true;
    bool m_horizontalSpacingLineThroughColumnSpacing = true;
    bool m_visualStateThroughColumnSpacing = true;
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
    DragFeedbackMode m_dragFeedbackMode = DragFeedbackMode::RowMoveAnimation;
    int m_dragReorderAnimationDuration = 180;
    QEasingCurve m_dragReorderEasingCurve = QEasingCurve::OutCubic;
    std::unique_ptr<RowDragPreview> m_dragRowPreview;
    QPersistentModelIndex m_dragPreviewSource;
    QVariantAnimation *m_dragReorderAnimation = nullptr;
    QElapsedTimer m_dragReorderClock;
    bool m_dragReorderFrameActive = false;
    /// Re-entrancy guard: a visual frame must not start another one.
    bool m_visualGeometryFrameActive = false;
    struct PreviewSpanCache {
        quint64 modelSerial = 0;
        quint64 mappingSerial = 0;
        quint64 providerSerial = 0;
        quint64 configurationSerial = 0;
        bool populated = false;
        QVector<QRect> rectangles;
    };
    std::weak_ptr<PreviewSpanCache> m_previewSpanCache;
    // The clip containers of a row widget are its children, tagged with their pane
    // index (see PaneClipHost): a recycled row widget can never leave a stale
    // pointer behind.
    bool m_ownHorizontalHeader = false;
    bool m_ownVerticalHeader = false;
    TableWidgetAdapter *m_tableAdapter = nullptr;
    bool m_ownTableAdapter = false;
    quint64 m_tableAdapterChangeSerial = 0;
    CellWidgetAdapter *m_cellAdapter = nullptr;
    bool m_ownCellAdapter = false;
    MaterializationMode m_materializationMode = MaterializationMode::RowWidgets;
    QHash<QPersistentModelIndex, QWidget *> m_cells;
    QHash<QWidget *, WidgetType> m_cellTypes;
    quint64 m_cellConfigurationSerial = 0;
    quint64 m_cellLifecycleSerial = 0;
    quint64 m_cellBindCount = 0;
    quint64 m_cellRecycleCount = 0;
    bool m_cellMaterializationActive = false;

    int m_headerHeight = 28;
    int m_verticalHeaderWidth = 56;
    bool m_horizontalHeaderVisible = true;
    bool m_verticalHeaderVisible = true;
    int m_columnOverscan = 1;
    int m_horizontalWheelPixels = 48;
    TableSpanProvider *m_spanProvider = nullptr;
    quint64 m_spanProviderSerial = 0;
    std::shared_ptr<TableSpanProvider> m_spanProviderOwner;
    // Reattaching an owned provider during its callback must reuse ownership.
    QHash<TableSpanProvider *, std::weak_ptr<TableSpanProvider>> m_spanProviderOwners;
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
    /// Opt-in resize gestures (see setColumnResizeEnabled()).
    bool m_columnResizeEnabled = false;
    bool m_verticalHeaderResizeEnabled = false;
    bool m_horizontalHeaderSelectionEnabled = false;
    bool m_verticalHeaderSelectionEnabled = true;
    bool m_horizontalHeaderVisualStateBackgroundVisible = true;
    bool m_verticalHeaderVisualStateBackgroundVisible = true;
    QPersistentModelIndex m_headerSelectionAnchor;
    QPersistentModelIndex m_headerSelectionCurrent;
    Qt::Orientation m_headerSelectionAnchorAxis = Qt::Vertical;
    /// Set once the application installed a model of its own: from then on the view never
    /// instantiates the row-drag model, whatever the switch does.
    bool m_applicationModelSeen = false;
    /// The ReorderableTableModel the view instantiated for the row drag (null while the
    /// application provided a model). Owned through its QObject parent.
    QPointer<QAbstractItemModel> m_internalModel;
};

} // namespace viv
