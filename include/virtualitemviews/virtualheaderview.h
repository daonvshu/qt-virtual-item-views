#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/headerwidgetadapter.h>
#include <virtualitemviews/headerview.h>

#include <QHash>
#include <QColor>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QWidget>

#include <functional>
#include <limits>

class QAbstractItemModel;
class QKeyEvent;
class QVariantAnimation;

namespace viv {

class HeaderGeometry;
class WidgetRecycler;

/// QWidget based header (architecture document §17-§19), for both axes.
///
/// Instead of letting QStyle paint sections, every *materialized* section is a
/// real QWidget produced by a HeaderWidgetAdapter, so a header can carry badges,
/// progress, filter buttons, search fields or animated sort arrows. It
/// deliberately does not inherit QHeaderView: the paint/cache machinery of
/// QHeaderView and a child widget tree do not mix well.
///
/// The same class renders the column header (Qt::Horizontal, sections packed along
/// x) and the row-number strip (Qt::Vertical, sections packed along y): everything
/// axis dependent goes through the small helpers below, so the resize gesture, the
/// drag reorder with its preview, the transition and the pane packing are written
/// once.
///
/// Invariants (§19):
///  - only visible sections (+ overscan) and pinned sections own a widget, so the
///    widget count never grows with the number of columns,
///  - a pooled widget is unbound and keeps no identity,
///  - section geometry always comes from HeaderGeometry, which stays the single
///    source of truth for sizes, order, visibility and the horizontal offset,
///  - a section whose child has focus or an open popup is pinned, not recycled
///    (§36).
class VIRTUALITEMVIEWS_EXPORT VirtualHeaderView : public QWidget, public HeaderViewInterface
{
    Q_OBJECT

public:
    /// \a orientation must match the HeaderGeometry it is bound to: a horizontal
    /// renderer packs its sections along x, a vertical one along y. Binding the other
    /// axis' geometry is refused (see setGeometryModel()).
    explicit VirtualHeaderView(Qt::Orientation orientation = Qt::Horizontal,
                               QWidget *parent = nullptr);
    ~VirtualHeaderView() override;

    // -- HeaderViewInterface -------------------------------------------------
    /// Binding a geometry of the other axis is refused (the renderer packs its sections
    /// along x, see the constructor).
    void setGeometryModel(HeaderGeometry *geometry) override;
    HeaderGeometry *geometryModel() const override { return m_geometry; }
    QWidget *headerWidget() override { return this; }
    Qt::Orientation orientation() const override { return m_orientation; }
    void setLabelModel(QAbstractItemModel *model) override;
    QAbstractItemModel *labelModel() const { return m_labelModel; }
    void setSortInteractionEnabled(bool enabled) override;
    bool isSortInteractionEnabled() const { return m_sortInteractionEnabled; }
    /// Restricts the header to one pane (§31): only \a logicalColumns are
    /// materialized and positioned, so the same class renders the scrollable
    /// pane and the frozen panes. \a frozen keeps the pane rect's own origin.
    void setPaneFilter(const QVector<int> &logicalColumns, bool frozen) override;
    void clearPaneFilter() override;
    bool hasPaneFilter() const { return m_paneFilterActive; }
    /// Offset of the pane's own content (§43 "advanced panes"): the sections are
    /// packed from the pane's left edge and shifted by \a offset, so a frozen pane
    /// (0) and a scrolling pane of a group other than the primary one stay aligned
    /// with the body. kFollowGeometryOffset (the default) keeps the header on the
    /// committed geometry, which is what a whole-table header uses.
    void setPaneOffset(qint64 offset) override;

    /// Visual geometry animation (§23/§24), on by default.
    ///
    /// A section move is the one interaction whose committed geometry is final
    /// before the user sees the result, so the renderer slides the section from its
    /// old position to the committed one instead of teleporting. Only the header
    /// moves: the body, the scroll bar and every geometry query read the committed
    /// HeaderGeometry, so they jump once - at the commit - and never follow an
    /// intermediate frame.
    ///
    /// Resizing a section, scrolling and pane changes stay immediate: those are the
    /// cases where the body *does* follow every frame (§24), so animating the
    /// header alone would tear header and body apart.
    void setSectionAnimationEnabled(bool enabled) override;
    bool sectionAnimationEnabled() const { return m_animationEnabled; }
    /// Duration of the visual transition in milliseconds; 0 disables it as well.
    void setSectionAnimationDuration(int ms) override;
    int sectionAnimationDuration() const { return m_animationDuration; }
    /// One-shot request for the next committed order change (§23): the renderer
    /// records where its sections are now and slides them to the new order. Without
    /// it an order change is applied at once, so a programmatic reorder never animates
    /// unless the application asks for it.
    void setSectionMoveAnimated(bool animated) override { m_animateOrderChange = animated; }

    /// Share of the leftover extent a section takes (see
    /// HeaderGeometry::setSectionStretchFactor()): 1 : 2 : 1 makes the third section twice
    /// the width of the first, and 0 - the default - a section that keeps its own size.
    ///
    /// The renderer only forwards: the widths live in HeaderGeometry, so the body follows
    /// every frame. Whoever owns the geometry still has to name the extent the sections
    /// fill - a VirtualTableView does that for its columns, and a *standalone* renderer
    /// keeps it equal to its own axis extent (a header inside a table is marked by
    /// setViewportOrigin() and leaves the target to the table).
    void setSectionStretchFactor(int logicalIndex, qreal factor);
    qreal sectionStretchFactor(int logicalIndex) const;
    /// Drag a section to a new position (both axes). **Off by default**: a drag changes
    /// the section order, so an application opts in.
    ///
    /// A *column* header writes the committed order into HeaderGeometry itself
    /// (`moveSection()`), so nothing else is needed; the row-number strip reports the move
    /// instead - the rows belong to the model (see
    /// VirtualTableView::setVerticalHeaderDragEnabled(), which also names the model half of
    /// that contract).
    ///
    /// The resize gesture (dragging a section edge) and a section click (sort) are separate
    /// gestures and are not affected by this switch.
    void setSectionDragEnabled(bool enabled) { m_sectionDragEnabled = enabled; }
    bool isSectionDragEnabled() const { return m_sectionDragEnabled; }
    /// Resize a section by dragging its leading/trailing edge (both axes: column width,
    /// row height). **On by default** - that is the gesture a header is expected to have
    /// (QHeaderView's Interactive mode) - and off means the edge is not special any more:
    /// the press behaves like one anywhere else in the section, and the resize cursor is
    /// not shown. The size APIs (`setColumnWidth()` / `setRowHeight()`) are unaffected.
    void setSectionResizeEnabled(bool enabled);
    bool isSectionResizeEnabled() const { return m_sectionResizeEnabled; }
    /// Tells the renderer that the section order does **not** belong to it: a committed
    /// drag then reports where the section would land (sectionMoveRequested()) instead
    /// of moving HeaderGeometry.
    ///
    /// The column header owns its order - HeaderGeometry is the single source of truth
    /// for it - so this stays off there. A row-number strip is the case it exists for:
    /// the order of the rows is the model's, and the strip can only ask for the move
    /// (the table turns the request into moveRows()).
    void setSectionOrderExternal(bool external) { m_externalSectionOrder = external; }
    bool hasExternalSectionOrder() const { return m_externalSectionOrder; }
    /// Origin of the viewport inside the view; the table sets it so section x
    /// positions can be derived from HeaderGeometry (viewport coordinates).
    void setViewportOrigin(const QPoint &origin) override;
    void setPaneTerminalColumn(int logicalIndex);
    /// Controls the trailing edge of the built-in section widgets and spacing gaps.
    void setSectionSeparatorsVisible(bool visible);
    bool sectionSeparatorsVisible() const { return m_sectionSeparatorsVisible; }
    /// Controls the edge perpendicular to the section sequence.
    void setCrossAxisSeparatorVisible(bool visible);
    bool crossAxisSeparatorVisible() const { return m_crossAxisSeparatorVisible; }
    /// Optional shared color supplied by the owning table.
    void setSectionSeparatorColor(const QColor &color);
    QColor sectionSeparatorColor() const { return m_sectionSeparatorColor; }
    void setCrossAxisSeparatorColor(const QColor &color);
    QColor crossAxisSeparatorColor() const { return m_crossAxisSeparatorColor; }
    void setSectionSeparatorWidth(int pixels);
    int sectionSeparatorWidth() const { return m_sectionSeparatorWidth; }
    void setCrossAxisSeparatorWidth(int pixels);
    int crossAxisSeparatorWidth() const { return m_crossAxisSeparatorWidth; }
    /// Visual section geometry (§23/§24): the x a section is *drawn* at while a
    /// drag preview or a transition is running, in viewport coordinates. False
    /// when the renderer is idle or the section owns no widget, so the caller
    /// falls back to the committed geometry - see the base class.
    bool sectionVisualX(int logicalIndex, int *viewportX) const override;
    bool hasVisualSectionGeometry() const override;
    void setVisualGeometryCallback(std::function<void()> callback) override;
    /// Re-reads the label of every materialized section (see the base class): a model-side
    /// row/column move leaves the section set alone, so the sections would otherwise keep
    /// the text of the item that used to sit there.
    void refreshSectionLabels() override;

    // -- adapter / diagnostics ----------------------------------------------
    void setAdapter(HeaderWidgetAdapter *adapter, bool takeOwnership = false);
    HeaderWidgetAdapter *adapter() const { return m_adapter; }
    WidgetRecycler *recycler() const { return m_recycler; }

    /// Sections that currently own a QWidget.
    QList<int> materializedSections() const;
    QWidget *sectionWidget(int logicalIndex) const;
    qsizetype materializedSectionCount() const { return m_sectionWidgets.size(); }
    qsizetype pooledSectionCount() const;
    /// How often the pane cache (membership set, committed visual order, prefix sums) was
    /// rebuilt. Diagnostics for the width-scroll tests: a scroll must not rebuild it.
    quint64 paneCacheRebuildCount() const { return m_paneCacheRebuilds; }
    /// Sections the materialization pass of the last relayout() looked at. Diagnostics for
    /// the wide-table tests: a pane has to be bounded by its own window, never by the width
    /// of the whole table (a pane of {0, 50000, 99999} must not scan 100,000 sections).
    qsizetype materializationVisits() const { return m_materializationVisits; }

    /// Views in front of/behind the viewport that are kept materialized.
    void setSectionOverscan(int sections);
    int sectionOverscan() const { return m_overscan; }

signals:
    /// Emitted *before* the current adapter is released (and before it is deleted when this
    /// renderer owns it). A collaborator that borrowed `adapter()` - the table's derived
    /// pane renderers do - has to drop its sections here, while the adapter is still alive.
    void adapterAboutToChange();
    /// Emitted after the new adapter is installed, so a collaborator can borrow it and
    /// rebuild what it dropped.
    void adapterChanged();
    /// A committed drag would put the section at visual index \a toVisual, but the
    /// order is not this renderer's to change (setSectionOrderExternal()): the drag is
    /// dropped back to the committed geometry and the request is reported instead.
    /// \a fromVisual / \a toVisual are visual indices - row numbers, for a row strip.
    void sectionMoveRequested(int fromVisual, int toVisual);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // -- axis helpers (§45: one renderer, two axes) ---------------------------
    bool isHorizontal() const { return m_orientation == Qt::Horizontal; }
    /// Component of \a point the sections are packed along.
    int axisOf(const QPoint &point) const
    {
        return isHorizontal() ? point.x() : point.y();
    }
    /// Position/size of \a widget along the axis (x/width, or y/height).
    int axisPosOf(const QWidget *widget) const
    {
        return isHorizontal() ? widget->x() : widget->y();
    }
    int axisSizeOf(const QWidget *widget) const
    {
        return isHorizontal() ? widget->width() : widget->height();
    }
    /// Extent of this renderer along its axis: the pack window and the section
    /// visibility test both use it.
    int axisExtent() const { return isHorizontal() ? width() : height(); }
    /// Places \a widget at \a pos/\a size along the axis, filling the cross axis.
    void placeSection(QWidget *widget, int pos, int size);
    /// Cursor of the resize gesture on this axis.
    Qt::CursorShape resizeCursor() const
    {
        return isHorizontal() ? Qt::SplitHCursor : Qt::SplitVCursor;
    }

    void connectGeometry(HeaderGeometry *geometry, bool connectSignals);
    /// Rebuilds the pane cache when it was invalidated (filter / geometry / visibility /
    /// order change) - never on a pure offset change. The cache is mutable, so the const
    /// query path (sectionPos) can fill it lazily like the derived caches of the layout do.
    void rebuildPaneCacheIfNeeded() const;
    /// Re-binds the materialized sections in the logical range [first, last] so a widget
    /// that is already on screen picks up a changed label or state.
    void rebindMaterializedSections(int first, int last);
    /// Keeps a standalone renderer's stretch target on its own axis extent (a table sets
    /// the target itself - see setSectionStretchFactor()).
    void syncStretchExtent();
    void relayout();
    void recycleAllSections();
    /// Value sectionPos() returns for a section this widget does not show. A pane
    /// offset may legitimately place a shown section at a negative x (a frozen
    /// pane shifted left, or a scrolling pane scrolled to its end), so the
    /// position itself cannot double as the "not shown" marker.
    static constexpr int kSectionNotShown = std::numeric_limits<int>::min();
    /// Position of \a logicalIndex along the renderer's axis, inside this widget
    /// (kSectionNotShown when hidden, filtered or unknown).
    int sectionPos(int logicalIndex) const;
    /// Places every materialized section, honouring the visual geometry (§23 while
    /// an animation runs, the committed geometry otherwise).
    void positionSections();
    /// Reports the placement above to the installed visual geometry callback: every
    /// frame that differs from the committed geometry, and once more on the frame
    /// that settles back onto it (an idle renderer says nothing).
    void notifyVisualGeometry();
    /// Places the sections while a drag preview is active: the dragged section
    /// follows the pointer, the others open / close the gap - all of it visual
    /// geometry, the committed order is not touched before the release (§22/§23).
    void positionDraggedSections();
    /// (Re)starts the "make room" tween when the insertion slot changes: every section
    /// keeps the position it has right now as its start and eases to its new slot with
    /// the same curve and duration as every other header animation (§23).
    void restartDragPreviewTween(int packedSlot);
    /// Starts the visual transition from the positions the materialized sections
    /// currently have to the committed ones.
    void animateSectionMove();
    /// Drag-reorder (§22): the press picks a section up, a threshold decides whether it
    /// is a drag or a click, and the release commits *once* so the visual transition can
    /// settle from the preview position.
    void beginSectionDrag(int logicalIndex, int pos);
    void updateSectionDrag(int pos);
    void finishSectionDrag(bool commit);
    /// The index the dragged section would land on, in final-order terms - that is the
    /// `to` argument of moveSection().
    int dragTargetIndex() const;
    /// Cursor for a position in header coordinates (§25): the resize cursor at a section
    /// edge, the ordinary arrow anywhere else. A gesture in progress (drag, resize) owns
    /// the cursor, so a hover never overwrites it.
    void updateCursor(const QPoint &pos);
    /// Makes \a root and everything inside it report their mouse position back to this
    /// header. The section widgets cover the header, so without this the header itself
    /// sees almost no mouse moves and the cursor sticks to whatever it was set to last -
    /// for the whole header, because children inherit the parent cursor.
    void watchMouse(QWidget *root);
    /// Logical columns in visual order, ignoring hidden and filtered ones.
    QVector<int> visualOrder() const;
    /// Read-only view of the packed order (visible sections in visual order). For a
    /// *uniform* geometry the order is the identity, so nothing is materialised: a drag on
    /// a ten-million-row strip must not build a ten-million-entry vector per mouse move
    /// (§7 of the vertical-header decision).
    struct PackedOrder
    {
        /// Empty for an identity order (a uniform geometry): the packed slot *is* the
        /// logical index then, and nothing was built.
        QVector<int> storage;
        int count = 0;
        bool isEmpty() const { return count <= 0; }
        int size() const { return count; }
        int at(int slot) const
        {
            if (slot < 0 || slot >= count)
                return -1;
            return storage.isEmpty() ? slot : storage.at(slot);
        }
        int indexOf(int logical) const
        {
            if (logical < 0 || logical >= count)
                return -1;
            return storage.isEmpty() ? logical : storage.indexOf(logical);
        }
    };
    PackedOrder packedOrder() const;
    /// True when this widget shows \a logicalIndex at all.
    bool showsSection(int logicalIndex) const;
    int sectionAt(const QPoint &pos) const;
    /// Logical section whose leading/trailing edge is under \a pos (or -1).
    int resizeEdgeAt(const QPoint &pos) const;
    bool isSectionPinned(int logicalIndex) const;
    bool isFiltered(int logicalIndex) const;

    Qt::Orientation m_orientation = Qt::Horizontal;
    /// Both are non-owning collaborators of a public standalone widget, so a business
    /// may delete either before the header: watched, so a dangling pointer cannot be
    /// dereferenced (P0-2 of the second review).
    QPointer<HeaderGeometry> m_geometry;
    QPointer<QAbstractItemModel> m_labelModel;
    HeaderWidgetAdapter *m_adapter = nullptr;
    bool m_ownAdapter = false;
    WidgetRecycler *m_recycler = nullptr;

    QHash<int, QWidget *> m_sectionWidgets;
    QWidget *m_dragLeadingSeparator = nullptr;
    QPoint m_viewportOrigin;
    QVector<int> m_paneFilter;
    bool m_paneFilterActive = false;
    qint64 m_paneOffset = kFollowGeometryOffset;
    int m_paneTerminalColumn = -1;
    bool m_sectionSeparatorsVisible = true;
    bool m_crossAxisSeparatorVisible = true;
    QColor m_sectionSeparatorColor;
    QColor m_crossAxisSeparatorColor;
    int m_sectionSeparatorWidth = 1;
    int m_crossAxisSeparatorWidth = 1;
    /// Pane cache (§31/§43, P1-8 of the second review): the pane's columns in committed
    /// visual order, the prefix sums of their widths, a logical → slot map and the
    /// membership set. Without it every sectionPos() rebuilt and sorted the pane's list and
    /// every isFiltered() scanned it, which made a pane-filtered header O(N^2) per pass.
    mutable QVector<int> m_paneOrder;
    mutable QVector<qint64> m_panePrefix;
    mutable QVector<int> m_paneSlotByLogical;
    mutable QSet<int> m_paneFilterSet;
    mutable bool m_paneCacheDirty = true;
    mutable quint64 m_paneCacheRebuilds = 0;
    /// Sections the last materialization pass looked at (diagnostics; see
    /// materializationVisits()).
    qsizetype m_materializationVisits = 0;
    /// Visual geometry (§23): visual x a section slides away from, and the
    /// progress of the transition (1 = committed geometry).
    QHash<int, int> m_slideFrom;
    qreal m_slideProgress = 1.0;
    QVariantAnimation *m_slideAnimation = nullptr;
    bool m_animationEnabled = true;
    int m_animationDuration = 300;
    /// One-shot gate (§24): only an order change the caller asked for is shown as a
    /// transition. A plain geometry change is applied immediately.
    bool m_animateOrderChange = false;
    /// Visual order of the last pass, to tell a section move (animate) from a
    /// resize or an offset change (immediate).
    QVector<int> m_lastVisualOrder;
    /// The last pass ran on a *uniform* geometry, whose order is the identity: kept as a
    /// flag plus the count instead of a vector, so a ten-million-row strip never
    /// materialises an order it does not have (see relayout()).
    bool m_lastOrderWasIdentity = false;
    int m_lastOrderCount = 0;
    /// HeaderGeometry::orderRevision() of the last pass: the order above is only
    /// re-derived when the geometry says it may have changed.
    quint32 m_lastOrderRevision = 0;
    /// True once a table told this renderer where its viewport starts; without that
    /// (a standalone header) its own client origin is the reference.
    bool m_viewportOriginSet = false;
    /// The section order belongs to somebody else (see setSectionOrderExternal()).
    bool m_externalSectionOrder = false;
    /// Drag-reorder state (§22/§23): the section the press picked up and where the
    /// pointer is. All of it is visual geometry - the committed order only changes on
    /// the release.
    int m_dragSection = -1;
    int m_dragStartPos = 0;
    int m_dragCurrentPos = 0;
    bool m_dragging = false;
    /// Preview easing (§22/§23): the sections that make room tween to their slot instead
    /// of teleporting. Same curve (OutCubic) and duration as the other header
    /// animations; the animation also runs while the pointer stands still, so the
    /// arrangement always finishes.
    QHash<int, int> m_previewFrom;
    qreal m_previewProgress = 1.0;
    QVariantAnimation *m_previewAnimation = nullptr;
    /// Insertion slot the running tween belongs to (-1 = none).
    int m_previewSlot = -1;
    /// Told about every visual placement frame (§23/§24); see the base class.
    std::function<void()> m_visualGeometryCallback;
    /// Whether the callback was last told "visual geometry is on screen"; lets the
    /// settling frame be reported without a report on every idle relayout.
    bool m_visualGeometryNotified = false;
    int m_overscan = 1;
    bool m_sortInteractionEnabled = false;
    /// See setSectionDragEnabled(): the reorder gesture is opt-in.
    bool m_sectionDragEnabled = false;
    /// See setSectionResizeEnabled(): resizing stays on unless it is switched off.
    bool m_sectionResizeEnabled = true;

    int m_resizeSection = -1;
    int m_resizeStartSize = 0;
    int m_resizeStartPos = 0;
    int m_pressedSection = -1;
    int m_pressedPos = 0;
    bool m_moved = false;
};

} // namespace viv
