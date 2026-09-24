#pragma once

#include <virtualitemviews/materializeditem.h>
#include <virtualitemviews/scrollmapper.h>
#include <virtualitemviews/types.h>

#include <QAbstractScrollArea>
#include <QHash>
#include <QItemSelectionModel>
#include <QList>
#include <QPersistentModelIndex>
#include <QSet>
#include <QVector>

class QAbstractItemModel;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

namespace viv {

class LayoutPolicy;
class WidgetAdapter;
class WidgetRecycler;

/// Snapshot of the virtualization state.
///
/// Designed for debug overlays and long-running validation: it is the data
/// behind "why does the widget count keep growing?" investigations.
struct VirtualViewStats
{
    /// Logical items offered by the model.
    qsizetype logicalItems = 0;
    /// Items that currently own a QWidget.
    qsizetype materializedItems = 0;
    /// Widgets waiting in the recycler pools.
    qsizetype pooledWidgets = 0;
    /// Materialized items that are pinned (focus, IME, popup, explicit pin).
    qsizetype pinnedWidgets = 0;

    /// Widgets created by the recycler since the view was constructed.
    quint64 createCount = 0;
    /// adapter->bindWidget() calls (one per (re)bind of an item).
    quint64 bindCount = 0;
    /// Widgets handed back to the recycler.
    quint64 recycleCount = 0;
};

/// Identity of the item that sits at the top of the viewport.
///
/// Used to keep the visual position stable when item sizes change above the
/// viewport (async images, expand/collapse, dataChanged with a new height).
struct ScrollAnchor
{
    QPersistentModelIndex index;
    int offsetInsideItem = 0;

    bool isValid() const { return index.isValid(); }
};

/// Virtualization kernel shared by the List/Table/Tree views.
///
/// The kernel owns the scroll space, the visible range computation, the
/// materialization and recycling of item widgets, the invalidation coalescing
/// and the selection/current handling. Subclasses only supply the identity
/// mapping (view row <-> QModelIndex) and a LayoutPolicy.
///
/// Invariants (see docs/architecture.md):
///  - materialized widgets == visible + overscan + pinned items,
///  - pooled widgets are unbound,
///  - one QModelIndex owns at most one widget at any time,
///  - model mutations never require a manual reload() from business code.
class VirtualItemView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    enum ScrollHint {
        EnsureVisible,
        PositionAtTop,
        PositionAtBottom,
        PositionAtCenter,
    };
    Q_ENUM(ScrollHint)

    explicit VirtualItemView(QWidget *parent = nullptr);
    ~VirtualItemView() override;

    // -- model ---------------------------------------------------------------
    virtual void setModel(QAbstractItemModel *model);
    QAbstractItemModel *model() const { return m_model; }

    // -- selection -----------------------------------------------------------
    void setSelectionModel(QItemSelectionModel *selectionModel);
    QItemSelectionModel *selectionModel() const { return m_selectionModel; }

    QModelIndex currentIndex() const;
    void setCurrentIndex(const QModelIndex &index);

    /// How selection gestures are interpreted (mirrors QAbstractItemView).
    enum class SelectionMode {
        NoSelection,
        SingleSelection,
        MultiSelection,
        ExtendedSelection,
    };
    Q_ENUM(SelectionMode)

    /// Which part of an item gets selected. SelectColumns arrives with
    /// VirtualTableView.
    enum class SelectionBehavior {
        SelectItems,
        SelectRows,
    };
    Q_ENUM(SelectionBehavior)

    void setSelectionMode(SelectionMode mode);
    SelectionMode selectionMode() const { return m_selectionMode; }
    void setSelectionBehavior(SelectionBehavior behavior);
    SelectionBehavior selectionBehavior() const { return m_selectionBehavior; }

    // -- adapter / recycler --------------------------------------------------
    void setAdapter(WidgetAdapter *adapter, bool takeOwnership = false);
    WidgetAdapter *adapter() const { return m_adapter; }
    WidgetRecycler *recycler() const { return m_recycler; }

    // -- virtualization ------------------------------------------------------
    /// Number of extra items materialized before/after the visible range.
    void setOverscan(int before, int after);
    int overscanBefore() const { return m_overscanBefore; }
    int overscanAfter() const { return m_overscanAfter; }

    void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible);

    /// Keeps the widget of \a index materialized even when it leaves the
    /// overscan window (business code can request a pin for async operations).
    void setItemPinned(const QModelIndex &index, bool pinned = true);
    bool isItemPinned(const QModelIndex &index) const;
    /// Pin/unpin the item that currently owns \a widget. Equivalent to
    /// setItemPinned(indexForWidget(widget), ...).
    void pinWidget(QWidget *widget);
    void unpinWidget(QWidget *widget);
    /// Soft cap for pinned items; exceeding it logs a qWarning once. Pinned
    /// items are never recycled automatically, so this is a debugging aid for
    /// "business code pins too much". A value <= 0 disables the check.
    void setMaxPinnedItems(int max);
    int maxPinnedItems() const { return m_maxPinnedItems; }
    /// Snapshot of the virtualization state (diagnostics / debug overlay).
    virtual VirtualViewStats stats() const;

    /// Opt-in bounded event log (create/bind/unbind/recycle/pin) used to debug
    /// "why does the widget count keep growing". Disabled by default.
    static constexpr qsizetype kLifecycleLogCapacity = 400;
    void setLifecycleLoggingEnabled(bool enabled);
    bool isLifecycleLoggingEnabled() const { return m_lifecycleLogEnabled; }
    QStringList lifecycleLog() const { return m_lifecycleLog; }
    void clearLifecycleLog();

    // -- lookup / geometry ---------------------------------------------------
    /// Index under a viewport position. Multi-column views override this to
    /// report the column as well.
    virtual QModelIndex indexAt(const QPoint &viewportPos) const;
    QRect visualRect(const QModelIndex &index) const;

    /// True when the kernel materializes item widgets itself. A view that
    /// materializes its own widgets (table cell mode) returns false and uses
    /// materializeItems()/rebindItemsInRange() instead.
    virtual bool usesItemWidgets() const;

    qint64 verticalOffset() const { return m_scrollOffset; }
    qint64 contentExtent() const;
    qint64 maximumVerticalOffset() const;

    ScrollMapper &scrollMapper() { return m_scrollMapper; }
    const ScrollMapper &scrollMapper() const { return m_scrollMapper; }

    /// Wheel scrolling granularity.
    /// Pixels (default) scrolls a fixed number of pixels per notch, so rows are
    /// never the unit of scrolling; Items keeps row-aligned wheel steps.
    enum class WheelScrollMode {
        Pixels,
        Items,
    };
    Q_ENUM(WheelScrollMode)

    void setWheelScrollMode(WheelScrollMode mode);
    WheelScrollMode wheelScrollMode() const { return m_wheelScrollMode; }
    /// Pixels per wheel notch in Pixels mode (default 48 = 3 lines x 16 px).
    void setWheelScrollPixels(int pixels);
    int wheelScrollPixels() const { return m_wheelScrollPixels; }
    /// Switches to Items mode: one notch scrolls \a items items.
    void setWheelScrollItems(int items);
    int wheelScrollItems() const { return m_wheelScrollItems; }

    /// Pixel-accurate programmatic scrolling (the logical axis unit is a pixel).
    void setVerticalOffset(qint64 offset);
    void scrollByPixels(qint64 pixels);

    /// How item sizes (row heights) are determined.
    enum class ItemHeightMode {
        /// All items share one height (FixedSizeIndex).
        Uniform,
        /// Items may differ (BlockSizeIndex + optional measurement feedback).
        Variable,
    };
    Q_ENUM(ItemHeightMode)

    void setItemHeightMode(ItemHeightMode mode);
    ItemHeightMode itemHeightMode() const { return m_heightMode; }
    /// Fixed height of every item. Passing 0 enables "fit to the first
    /// materialized item" (requires setAutoMeasureItemHeight(true)).
    void setUniformItemHeight(int height);
    int uniformItemHeight() const { return m_uniformItemHeight; }
    /// Height used for items that were never measured in Variable mode.
    void setEstimatedItemHeight(int height);
    int estimatedItemHeight() const { return m_estimatedItemHeight; }
    /// Enables measuring the size hint of bound widgets (Variable mode) and the
    /// first-item fitting of Uniform mode.
    void setAutoMeasureItemHeight(bool enabled);
    bool autoMeasureItemHeight() const { return m_autoMeasure; }

    /// Visible item range without overscan.
    VisibleRange visibleItemRange() const;

    // -- diagnostics (tests, benchmarks, demos) ------------------------------
    qsizetype materializedItemCount() const { return m_items.size(); }
    qsizetype pinnedItemCount() const;
    QWidget *widgetForIndex(const QModelIndex &index) const;
    QModelIndex indexForWidget(const QWidget *widget) const;
    const QList<MaterializedItem> &materializedItems() const { return m_items; }

    qsizetype pooledWidgetCount() const;
    qsizetype createdWidgetCount() const;
    qsizetype destroyedWidgetCount() const;

    bool isRelayoutPending() const { return m_relayoutScheduled; }
    /// Applies a pending invalidation synchronously (used by tests and by
    /// code that needs a settled geometry before measuring).
    void flushPendingRelayout();

signals:
    void clicked(const QModelIndex &index);
    void doubleClicked(const QModelIndex &index);
    void activated(const QModelIndex &index);
    /// Emitted after every completed materialization pass.
    void virtualizationUpdated();

protected:
    // -- subclass contract ---------------------------------------------------
    /// Number of items the view can materialize (rows of a list, visible
    /// flattened rows of a tree).
    virtual qsizetype viewItemCount() const = 0;
    virtual QModelIndex viewIndex(qsizetype item, int column = 0) const = 0;
    /// View item of a model index, or -1 when the index is not materializable.
    virtual qsizetype viewItemForIndex(const QModelIndex &index) const = 0;
    /// True when \a parent owns the items of the layout.
    virtual bool isLayoutParent(const QModelIndex &parent) const = 0;
    /// Tree depth of an index; used by the future tree layout.
    virtual int itemDepth(const QModelIndex &index) const;
    /// Index used when the current item moves to \a item. The default is
    /// viewIndex(item); a table keeps the current column instead.
    virtual QModelIndex indexForNavigation(qsizetype item, const QModelIndex &current) const;
    /// Estimated size of an item that has never been measured (0 = layout
    /// estimate).
    virtual int estimateItemSize(qsizetype item) const;
    /// Hook called after a materialization pass (measurement feedback).
    virtual void afterMaterialize();
    /// Called instead of the built-in widget materialization when
    /// usesItemWidgets() is false: \a rows is the materialization window
    /// (visible rows widened by the overscan).
    virtual void materializeItems(const VisibleRange &rows);
    /// Rebuilds the widgets affected by a dataChanged() range. The default
    /// implementation rebinds the row widgets.
    virtual void rebindItemsInRange(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    /// True when \a widget (or one of its children/popups) owns the focus, that
    /// is: when it must not be recycled.
    bool hasFocusWithin(const QWidget *widget) const;
    /// Recycles the materialized items whose index lies in the model range
    /// (identity based, so it also works for a tree).
    void recycleItemsInModelRange(const QModelIndex &parent, int first, int last);
    /// Moves the current item to view row \a item and ensures it is visible.
    void moveCurrentToItem(qsizetype item, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    /// Hook that lets a subclass veto the automatic height measurement of an
    /// item (for example a table row whose height the user set explicitly).
    virtual bool canMeasureItem(qsizetype item) const;
    /// Hook for subclass specific keys (tree expand/collapse). Returning true
    /// consumes the event.
    virtual bool handleItemKeyPress(QKeyEvent *event);

    // -- kernel API for subclasses -------------------------------------------
    LayoutPolicy *layoutPolicy() const { return m_layout; }
    void setLayoutPolicy(LayoutPolicy *policy, bool takeOwnership = true);
    /// Rebuilds the layout item count from the model (after a model change).
    void resetLayoutForNewModel();

    /// Marks the view dirty and coalesces the relayout into one event loop pass.
    void markDirty();
    /// Runs a materialization pass immediately.
    void relayout();

    ScrollAnchor captureAnchor() const;
    void setPendingAnchor(const ScrollAnchor &anchor);
    void cancelPendingAnchor();
    void recycleAllItems();

    int viewportMainExtent() const;
    /// Viewport geometry of a view row. The default is the layout's item rect; a
    /// tree insets it by the item depth (indentation).
    virtual QRect geometryForViewRow(qsizetype row) const;
    /// Number of materialized (visible + overscan) rows, used by paging keys.
    qsizetype visibleItemCount() const;
    /// Range of items intersecting the viewport (no overscan).
    VisibleRange coreVisibleRange() const;
    /// Recreates the SizeIndex matching the current ItemHeightMode.
    void rebuildSizeIndex();
    int measuredHeightOf(const MaterializedItem &item) const;

    // -- events --------------------------------------------------------------
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void connectModel(QAbstractItemModel *model);
    void disconnectModel(QAbstractItemModel *model);
    void scheduleRelayout();
    void applyPendingAnchor();
    void syncScrollBars();
    void rebuildLookup();

    MaterializedItem createItem(const QPersistentModelIndex &index);
    void recycleItem(MaterializedItem &item);
    bool isPinnedItem(const MaterializedItem &item) const;
    /// True when \a widget (or one of its children) owns the focus or an active
    /// popup, that is: when it must not be recycled.
    void rebindItemsInModelRange(const QModelIndex &parent, int first, int last);
    void checkPinLimit();
    void moveCurrentTo(qsizetype row, Qt::KeyboardModifiers modifiers);
    void updateSelectionForClick(const QModelIndex &index, Qt::KeyboardModifiers modifiers);
    void toggleClickedIndex(const QModelIndex &index);
    void extendSelectionTo(const QModelIndex &index);
    /// Re-asserts the current index after a selection command whose Rows/Columns
    /// flag may have moved it to the start of the row/column.
    void pinCurrentIndex(const QModelIndex &index);
    QItemSelectionModel::SelectionFlags rowFlags() const;
    void appendLifecycleLog(const QString &entry);
    qint64 wheelStepPixels() const;
    qint64 scrollBarSingleStepPixels() const;

    /// Qt 5 declares the roles argument of
    /// QAbstractItemModel::dataChanged() as QVector<int>, Qt 6 as QList<int>.
    /// QVector is an alias of QList in Qt 6, so this signature matches both.
    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles);
    void onRowsAboutToBeInserted(const QModelIndex &parent, int first, int last);
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last);
    void onRowsRemoved(const QModelIndex &parent, int first, int last);
    void onRowsAboutToBeMoved(const QModelIndex &sourceParent, int start, int end,
                              const QModelIndex &destParent, int row);
    void onRowsMoved(const QModelIndex &sourceParent, int start, int end,
                     const QModelIndex &destParent, int row);
    void onLayoutAboutToBeChanged(const QList<QPersistentModelIndex> &parents,
                                  QAbstractItemModel::LayoutChangeHint hint);
    void onLayoutChanged(const QList<QPersistentModelIndex> &parents,
                         QAbstractItemModel::LayoutChangeHint hint);
    void onModelAboutToBeReset();
    void onModelReset();

    QAbstractItemModel *m_model = nullptr;
    QItemSelectionModel *m_selectionModel = nullptr;
    bool m_ownSelectionModel = false;
    WidgetAdapter *m_adapter = nullptr;
    bool m_ownAdapter = false;
    LayoutPolicy *m_layout = nullptr;
    bool m_ownLayout = false;
    WidgetRecycler *m_recycler = nullptr;

    QList<MaterializedItem> m_items;
    QHash<QPersistentModelIndex, qsizetype> m_itemLookup;
    QSet<QPersistentModelIndex> m_explicitPinned;

    ScrollMapper m_scrollMapper;
    qint64 m_scrollOffset = 0;
    ScrollAnchor m_pendingAnchor;
    bool m_anchorPending = false;
    QPersistentModelIndex m_pressedIndex;
    /// Row where a Shift+click/Shift+arrow selection started.
    QPersistentModelIndex m_selectionAnchor;

    quint64 m_bindCount = 0;
    quint64 m_recycleCount = 0;
    int m_maxPinnedItems = 0;
    bool m_pinLimitWarned = false;

    SelectionMode m_selectionMode = SelectionMode::ExtendedSelection;
    SelectionBehavior m_selectionBehavior = SelectionBehavior::SelectItems;
    bool m_lifecycleLogEnabled = false;
    QStringList m_lifecycleLog;

    int m_overscanBefore = 2;
    int m_overscanAfter = 2;
    WheelScrollMode m_wheelScrollMode = WheelScrollMode::Pixels;
    int m_wheelScrollPixels = 48;
    int m_wheelScrollItems = 3;

    ItemHeightMode m_heightMode = ItemHeightMode::Uniform;
    int m_uniformItemHeight = 32;
    int m_estimatedItemHeight = 32;
    bool m_autoMeasure = true;
    int m_measurePasses = 0;

    bool m_inRelayout = false;
    bool m_relayoutScheduled = false;
};

} // namespace viv
