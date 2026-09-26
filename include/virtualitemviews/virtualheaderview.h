#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/headerwidgetadapter.h>
#include <virtualitemviews/nativeheaderview.h>

#include <QHash>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QWidget>

#include <limits>

class QAbstractItemModel;
class QKeyEvent;
class QVariantAnimation;

namespace viv {

class HeaderGeometry;
class WidgetRecycler;

/// QWidget based header (architecture document §17-§19).
///
/// Instead of letting QStyle paint sections, every *materialized* section is a
/// real QWidget produced by a HeaderWidgetAdapter, so a header can carry badges,
/// progress, filter buttons, search fields or animated sort arrows. It
/// deliberately does not inherit QHeaderView: the paint/cache machinery of
/// QHeaderView and a child widget tree do not mix well.
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
    /// \a orientation must be Qt::Horizontal: the renderer packs its sections along x
    /// from a horizontal HeaderGeometry. A vertical instance warns and stays empty;
    /// use NativeHeaderView(Qt::Vertical) for the row-number strip.
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
    /// Origin of the viewport inside the view; the table sets it so section x
    /// positions can be derived from HeaderGeometry (viewport coordinates).
    void setViewportOrigin(const QPoint &origin) override;

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

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void connectGeometry(HeaderGeometry *geometry, bool connectSignals);
    /// Rebuilds the pane cache when it was invalidated (filter / geometry / visibility /
    /// order change) - never on a pure offset change. The cache is mutable, so the const
    /// query path (sectionX) can fill it lazily like the derived caches of the layout do.
    void rebuildPaneCacheIfNeeded() const;
    /// Re-binds the materialized sections in the logical range [first, last] so a widget
    /// that is already on screen picks up a changed label or state.
    void rebindMaterializedSections(int first, int last);
    void relayout();
    void recycleAllSections();
    /// Value sectionX() returns for a section this widget does not show. A pane
    /// offset may legitimately place a shown section at a negative x (a frozen
    /// pane shifted left, or a scrolling pane scrolled to its end), so the
    /// position itself cannot double as the "not shown" marker.
    static constexpr int kSectionNotShown = std::numeric_limits<int>::min();
    /// x of \a logicalIndex inside this widget (kSectionNotShown when hidden,
    /// filtered or unknown).
    int sectionX(int logicalIndex) const;
    /// Places every materialized section, honouring the visual geometry (§23 while
    /// an animation runs, the committed geometry otherwise).
    void positionSections();
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
    void beginSectionDrag(int logicalIndex, int x);
    void updateSectionDrag(int x);
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
    QPoint m_viewportOrigin;
    QVector<int> m_paneFilter;
    bool m_paneFilterActive = false;
    qint64 m_paneOffset = kFollowGeometryOffset;
    /// Pane cache (§31/§43, P1-8 of the second review): the pane's columns in committed
    /// visual order, the prefix sums of their widths, a logical → slot map and the
    /// membership set. Without it every sectionX() rebuilt and sorted the pane's list and
    /// every isFiltered() scanned it, which made a pane-filtered header O(N^2) per pass.
    mutable QVector<int> m_paneOrder;
    mutable QVector<qint64> m_panePrefixX;
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
    /// HeaderGeometry::orderRevision() of the last pass: the order above is only
    /// re-derived when the geometry says it may have changed.
    quint32 m_lastOrderRevision = 0;
    /// True once a table told this renderer where its viewport starts; without that
    /// (a standalone header) its own client origin is the reference.
    bool m_viewportOriginSet = false;
    /// Drag-reorder state (§22/§23): the section the press picked up and where the
    /// pointer is. All of it is visual geometry - the committed order only changes on
    /// the release.
    int m_dragSection = -1;
    int m_dragStartX = 0;
    int m_dragCurrentX = 0;
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
    int m_overscan = 1;
    bool m_sortInteractionEnabled = false;

    int m_resizeSection = -1;
    int m_resizeStartSize = 0;
    int m_resizeStartX = 0;
    int m_pressedSection = -1;
    int m_pressedX = 0;
    bool m_moved = false;
};

} // namespace viv
