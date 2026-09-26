#include <virtualitemviews/virtualheaderview.h>

#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/widgetrecycler.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QChildEvent>
#include <QCursor>
#include <QEasingCurve>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSet>
#include <QVariantAnimation>

#include <algorithm>

namespace viv {

namespace {
/// Pixels at a section edge that start a resize instead of a click/move (§25).
constexpr int kResizeMargin = 3;

/// How far outside the viewport a section x is still reported exactly (same convention as
/// the geometry and the pane layout): a widget position is int based, so a column of an
/// explicitly scrolled pane must not turn into a huge (or overflowing) coordinate.
constexpr qint64 kMaxOffscreenX = qint64(1) << 20;

inline QPoint eventPosition(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}
} // namespace

VirtualHeaderView::VirtualHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    // The renderer packs its sections along the x axis: it derives every section's x
    // from the (horizontal) HeaderGeometry. A vertical instance would silently stay
    // empty, so it is refused loudly instead (P1-14). The row-number strip is either
    // a native QHeaderView (NativeHeaderView) or a custom HeaderViewInterface.
    if (orientation != Qt::Horizontal) {
        qWarning("VirtualHeaderView: only Qt::Horizontal is supported; a vertical section "
                 "renderer stays empty - use NativeHeaderView for the row-number strip");
    }
    m_recycler = new WidgetRecycler(this);
    m_recycler->setParentWidget(this);
    m_recycler->setFactory([this](WidgetType type, QWidget *parent) -> QWidget * {
        return m_adapter ? m_adapter->createSection(type, parent) : nullptr;
    });
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // "Making room" for the dragged section is a tween of its own: driven by mouse moves
    // plus this animation, so it also finishes while the pointer stands still, and it
    // uses the same curve and duration as every other header animation (§22/§23).
    m_previewAnimation = new QVariantAnimation(this);
    m_previewAnimation->setStartValue(0.0);
    m_previewAnimation->setEndValue(1.0);
    m_previewAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_previewAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                m_previewProgress = value.toReal();
                if (m_dragging)
                    positionSections();
            });
    connect(m_previewAnimation, &QVariantAnimation::finished, this, [this]() {
        if (m_previewProgress < 1.0)
            return; // stopped before the end, not finished
        m_previewProgress = 1.0;
        m_previewFrom.clear();
        if (m_dragging)
            positionSections();
    });
}

VirtualHeaderView::~VirtualHeaderView()
{
    // Widgets must be unbound while the adapter is still alive.
    recycleAllSections();
    if (m_ownAdapter)
        delete m_adapter;
}

// ---------------------------------------------------------------------------
// HeaderViewInterface
// ---------------------------------------------------------------------------

void VirtualHeaderView::setGeometryModel(HeaderGeometry *geometry)
{
    if (m_geometry == geometry)
        return;
    if (geometry && geometry->orientation() != m_orientation) {
        qWarning("VirtualHeaderView::setGeometryModel(): the geometry belongs to the other "
                 "orientation; ignored");
        return;
    }
    connectGeometry(m_geometry, false);
    m_geometry = geometry;
    // The pane cache, the remembered visual order and its revision all describe the geometry
    // that was just replaced (P1 of the third review - a standalone header may switch
    // geometries through the public API).
    m_paneCacheDirty = true;
    m_lastVisualOrder.clear();
    m_lastOrderRevision = 0;
    connectGeometry(m_geometry, true);
    // The geometry is a collaborator of the adapter too (a section widget that draws sort state
    // reads its sort indicator): tell it which geometry the sections are laid out with, exactly
    // like setLabelModel() does for the labels (P1.3 of the fourth review).
    if (m_adapter)
        m_adapter->setGeometryModel(m_geometry.data());
    relayout();
}

void VirtualHeaderView::connectGeometry(HeaderGeometry *geometry, bool connectSignals)
{
    if (!geometry)
        return;
    if (connectSignals) {
        // A destroyed collaborator is reported to the adapter as "no geometry" - the header's
        // QPointer goes null at the same moment, so both sides agree without guessing
        // (P1.3 of the fourth review).
        connect(geometry, &QObject::destroyed, this, [this]() {
            if (m_adapter)
                m_adapter->setGeometryModel(nullptr);
        });
        // Any of these changes moves sections or changes which ones own a widget.
        connect(geometry, &HeaderGeometry::geometryChanged, this, [this]() {
            // Widths, visibility, order and the section set may all have changed: the pane
            // cache (membership set, packing order, prefix sums) is invalidated here and
            // rebuilt on the next pass - a pure scroll only emits offsetChanged.
            m_paneCacheDirty = true;
            relayout();
        });
        connect(geometry, &HeaderGeometry::sectionCountChanged, this, [this](int) { relayout(); });
        connect(geometry, &HeaderGeometry::offsetChanged, this, [this](qint64) { relayout(); });
        // Bulk changes (a restored state, the size range, the default size) carry no granular
        // signal, so the visible section UI is re-bound from the geometry: a custom renderer
        // that draws a sort badge or a state label is then up to date even when the sort
        // state itself did not change (P1 of the third review).
        connect(geometry, &HeaderGeometry::bulkGeometryChanged, this, [this]() {
            rebindMaterializedSections(std::numeric_limits<int>::min(),
                                       std::numeric_limits<int>::max());
        });
        // The sort indicator belongs to the geometry but is drawn by the section widget
        // the business builds in bindSection(), so the change has to reach the sections
        // that are already on screen (both the old and the new sorted one).
        connect(geometry, &HeaderGeometry::sortIndicatorChanged, this,
                [this](int, Qt::SortOrder) {
                    rebindMaterializedSections(std::numeric_limits<int>::min(),
                                               std::numeric_limits<int>::max());
                });
    } else {
        disconnect(geometry, nullptr, this, nullptr);
    }
}

void VirtualHeaderView::setLabelModel(QAbstractItemModel *model)
{
    if (m_labelModel == model)
        return;
    if (m_labelModel)
        disconnect(m_labelModel, nullptr, this, nullptr);
    m_labelModel = model;
    if (m_labelModel) {
        // A label model that is destroyed underneath the header is reported to the adapter as
        // "no model": the adapter then holds no pointer that it would have to clear itself
        // (P1.3 of the fourth review; the header's own QPointer covers the header side).
        connect(m_labelModel, &QObject::destroyed, this, [this]() {
            if (m_adapter)
                m_adapter->setLabelModel(nullptr);
        });
        // A rename touches only the named sections, so those are rebound in place. A
        // structural change moves the logical identity of every section: the materialized
        // widgets are recycled on the *about to* signal (unbound while their old identity
        // is still the valid one, see recycleAllSections()) and acquired again afterwards.
        // Relayout alone was not enough: it only binds newly acquired widgets, so an
        // already materialized section kept the label and identity it was bound with
        // (P0-3 of the second review).
        connect(m_labelModel, &QAbstractItemModel::headerDataChanged, this,
                [this](Qt::Orientation orientation, int first, int last) {
                    if (orientation != m_orientation)
                        return;
                    rebindMaterializedSections(first, last);
                });
        connect(m_labelModel, &QAbstractItemModel::columnsAboutToBeInserted, this,
                [this](const QModelIndex &, int, int) { recycleAllSections(); });
        connect(m_labelModel, &QAbstractItemModel::columnsAboutToBeRemoved, this,
                [this](const QModelIndex &, int, int) { recycleAllSections(); });
        connect(m_labelModel, &QAbstractItemModel::columnsAboutToBeMoved, this,
                [this](const QModelIndex &, int, int, const QModelIndex &, int) {
                    recycleAllSections();
                });
        connect(m_labelModel, &QAbstractItemModel::modelAboutToBeReset, this,
                [this]() { recycleAllSections(); });
        connect(m_labelModel, &QAbstractItemModel::modelReset, this, [this]() { relayout(); });
        connect(m_labelModel, &QAbstractItemModel::columnsInserted, this,
                [this](const QModelIndex &, int, int) { relayout(); });
        connect(m_labelModel, &QAbstractItemModel::columnsRemoved, this,
                [this](const QModelIndex &, int, int) { relayout(); });
        connect(m_labelModel, &QAbstractItemModel::columnsMoved, this,
                [this](const QModelIndex &, int, int, const QModelIndex &, int) { relayout(); });
    }
    // The label model is part of the section binding contract: an adapter that captured the
    // model (the README example does) is told about the switch, and the sections that are
    // already materialized are re-bound from the new model instead of keeping the old titles
    // (P1 of the third review).
    if (m_adapter)
        m_adapter->setLabelModel(model);
    rebindMaterializedSections(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    relayout();
}

void VirtualHeaderView::rebindMaterializedSections(int first, int last)
{
    if (!m_adapter)
        return;
    for (auto it = m_sectionWidgets.cbegin(); it != m_sectionWidgets.cend(); ++it) {
        if (it.key() < first || it.key() > last)
            continue;
        // Re-binding is also how the business learns about the change, so this is the
        // contract for "the state of this section changed" (sort indicator included).
        m_adapter->bindSection(it.value(), it.key());
    }
}

void VirtualHeaderView::setSortInteractionEnabled(bool enabled)
{
    m_sortInteractionEnabled = enabled;
}

void VirtualHeaderView::setPaneFilter(const QVector<int> &logicalColumns, bool frozen)
{
    Q_UNUSED(frozen);
    // Pane headers are re-installed by the table on every pass (syncHeaderPanes()), so the
    // equality fast path matters: an unchanged filter used to re-enter relayout() - and, in
    // pane mode, rebuild the pane cache - on every scroll step (P1-8 of the second review).
    if (m_paneFilterActive && m_paneFilter == logicalColumns)
        return;
    m_paneFilter = logicalColumns;
    m_paneFilterActive = true;
    m_paneCacheDirty = true;
    relayout();
}

void VirtualHeaderView::clearPaneFilter()
{
    if (!m_paneFilterActive)
        return;
    m_paneFilterActive = false;
    m_paneFilter.clear();
    m_paneOffset = kFollowGeometryOffset;
    m_paneCacheDirty = true;
    relayout();
}

void VirtualHeaderView::setPaneOffset(qint64 offset)
{
    if (m_paneOffset == offset)
        return;
    m_paneOffset = offset;
    relayout();
}

void VirtualHeaderView::setSectionAnimationEnabled(bool enabled)
{
    if (m_animationEnabled == enabled)
        return;
    m_animationEnabled = enabled;
    if (!enabled) {
        // Whatever is in flight lands on the committed geometry now.
        if (m_slideAnimation)
            m_slideAnimation->stop();
        m_slideFrom.clear();
        m_slideProgress = 1.0;
        positionSections();
    }
}

void VirtualHeaderView::setSectionAnimationDuration(int ms)
{
    m_animationDuration = qMax(0, ms);
}

void VirtualHeaderView::setViewportOrigin(const QPoint &origin)
{
    m_viewportOriginSet = true;
    if (m_viewportOrigin == origin)
        return;
    m_viewportOrigin = origin;
    relayout();
}

void VirtualHeaderView::setAdapter(HeaderWidgetAdapter *adapter, bool takeOwnership)
{
    if (m_adapter == adapter) {
        m_ownAdapter = m_ownAdapter || takeOwnership;
        return;
    }
    // Collaborators that borrowed the old adapter (the table's pane renderers) have to
    // release their sections first: they unbind through that adapter, which is still alive
    // here but may be deleted a few lines below (P0-1 of the third review).
    emit adapterAboutToChange();
    // Same rule as the view: unbind with the old adapter, drop its pooled section
    // widgets (they belong to another adapter's WidgetType namespace), and only
    // then let the old adapter be deleted.
    recycleAllSections();
    if (m_recycler)
        m_recycler->clear();
    if (m_ownAdapter) {
        delete m_adapter;
        m_adapter = nullptr;
    }
    m_adapter = adapter;
    m_ownAdapter = adapter && takeOwnership;
    // The label model is part of the section binding contract (see setLabelModel()): an
    // adapter that captured the model - the README example does - has to be told the current
    // one here as well, otherwise "setLabelModel() then setAdapter()" and
    // "setAdapter() then setLabelModel()" would behave differently (P1.1 of the fourth review).
    if (m_adapter)
        m_adapter->setLabelModel(m_labelModel.data());
    if (m_adapter)
        m_adapter->setGeometryModel(m_geometry.data());
    relayout();
    emit adapterChanged();
}

void VirtualHeaderView::setSectionOverscan(int sections)
{
    const int clamped = qMax(0, sections);
    if (m_overscan == clamped)
        return;
    m_overscan = clamped;
    relayout();
}

// ---------------------------------------------------------------------------
// Materialization (§19)
// ---------------------------------------------------------------------------

bool VirtualHeaderView::isFiltered(int logicalIndex) const
{
    return m_paneFilterActive && !m_paneFilterSet.contains(logicalIndex);
}

void VirtualHeaderView::rebuildPaneCacheIfNeeded() const
{
    if (!m_paneCacheDirty)
        return;
    m_paneCacheDirty = false;
    ++m_paneCacheRebuilds;
    m_paneOrder.clear();
    m_panePrefixX.clear();
    m_paneSlotByLogical.clear();
    m_paneFilterSet.clear();
    if (!m_geometry || !m_paneFilterActive)
        return;

    const int count = m_geometry->sectionCount();
    m_paneFilterSet.reserve(m_paneFilter.size());
    for (int logical : m_paneFilter) {
        if (logical >= 0 && logical < count)
            m_paneFilterSet.insert(logical);
    }

    // The pane shows its columns in the *committed* visual order, so the order has to be
    // derived once per geometry change - not once per sectionX() call.
    m_paneOrder.reserve(m_paneFilterSet.size());
    for (int logical : m_paneFilterSet) {
        if (!m_geometry->isSectionHidden(logical))
            m_paneOrder.append(logical);
    }
    std::sort(m_paneOrder.begin(), m_paneOrder.end(), [this](int lhs, int rhs) {
        return m_geometry->visualIndex(lhs) < m_geometry->visualIndex(rhs);
    });

    m_paneSlotByLogical.fill(-1, count);
    m_panePrefixX.reserve(m_paneOrder.size() + 1);
    m_panePrefixX.append(0);
    qint64 x = 0;
    for (int slot = 0; slot < m_paneOrder.size(); ++slot) {
        const int logical = m_paneOrder.at(slot);
        m_paneSlotByLogical[logical] = slot;
        x += m_geometry->sectionSize(logical);
        m_panePrefixX.append(x);
    }
}

bool VirtualHeaderView::showsSection(int logicalIndex) const
{
    if (!m_geometry || logicalIndex < 0 || isFiltered(logicalIndex))
        return false;
    const ColumnGeometry geometry = m_geometry->columnGeometry(logicalIndex);
    if (!geometry.isValid() || geometry.hidden)
        return false;
    return true;
}

int VirtualHeaderView::sectionX(int logicalIndex) const
{
    if (!showsSection(logicalIndex))
        return kSectionNotShown;
    const ColumnGeometry geometry = m_geometry->columnGeometry(logicalIndex);
    if (m_paneFilterActive && m_paneOffset != kFollowGeometryOffset) {
        // Pane layout (§43 "advanced panes"): a pane packs *its own* columns from
        // its own left edge and shifts them by its own offset, so a frozen pane
        // and a scrolling pane of a non-primary group stay aligned with the body.
        // The pane's columns are not necessarily a contiguous slice of the committed
        // order, so the packing comes from the pane cache (prefix sums over the pane's
        // own columns) instead of being rebuilt here (P1-8 of the second review).
        rebuildPaneCacheIfNeeded();
        const int slot = logicalIndex < m_paneSlotByLogical.size()
            ? m_paneSlotByLogical.at(logicalIndex)
            : -1;
        if (slot < 0)
            return kSectionNotShown;
        const qint64 localX = m_panePrefixX.at(slot);
        // A pane of an explicit list may be scrolled arbitrarily far, so the value is
        // clamped like the off-screen column x of the geometry (QWidget/QRect arithmetic is
        // int based).
        return int(qBound<qint64>(-kMaxOffscreenX, localX - m_paneOffset, kMaxOffscreenX));
    }
    // HeaderGeometry is in viewport coordinates and this widget is placed inside the
    // view (normally on a pane rect), so its own origin has to be subtracted.
    // Without a table (a standalone header) there is no view coordinate space: the
    // widget's own client origin is the reference, and nothing is subtracted.
    const int ownOriginX = m_viewportOriginSet ? x() : 0;
    return geometry.viewportX + m_viewportOrigin.x() - ownOriginX;
}

void VirtualHeaderView::relayout()
{
    if (m_orientation != Qt::Horizontal || !m_geometry || !m_adapter
        || m_geometry->sectionCount() <= 0) {
        recycleAllSections();
        m_lastVisualOrder.clear();
        update();
        return;
    }

    // The pane cache backs isFiltered() and sectionX(); it is rebuilt once per filter or
    // geometry change here, never inside those calls (P1-8 of the second review).
    rebuildPaneCacheIfNeeded();

    const int count = qMin(m_geometry->sectionCount(),
                           m_labelModel ? m_labelModel->columnCount() : m_geometry->sectionCount());
    if (count <= 0) {
        recycleAllSections();
        m_lastVisualOrder.clear();
        update();
        return;
    }

    // §23/§24: only an order change the caller asked for is shown as a transition
    // (setSectionMoveAnimated()), everything else is immediate. A programmatic
    // reorder therefore never animates unless the application requests it, and
    // resizing, hiding and scrolling - where the body follows every frame - stay
    // frame-synchronous as well.
    // The order of the visible sections is only re-derived when the geometry says
    // it may have changed (orderRevision) or when a transition was requested:
    // relayout() also runs on every scroll, and rebuilding the full order there
    // made a 20,000 column header walk 20,000 columns per wheel step.
    const quint32 orderRevision = m_geometry->orderRevision();
    bool sectionsReordered = false;
    if (m_animateOrderChange || m_lastVisualOrder.isEmpty()
        || orderRevision != m_lastOrderRevision) {
        const QVector<int> order = visualOrder();
        if (!m_lastVisualOrder.isEmpty() && m_lastVisualOrder.size() == order.size()) {
            QVector<int> before = m_lastVisualOrder;
            QVector<int> after = order;
            std::sort(before.begin(), before.end());
            std::sort(after.begin(), after.end());
            sectionsReordered = before == after && m_lastVisualOrder != order;
        }
        m_lastVisualOrder = order;
    }
    m_lastOrderRevision = orderRevision;
    const bool animateMove = sectionsReordered && m_animateOrderChange;
    m_animateOrderChange = false; // the request is consumed by this pass

    // 1) Visual range of the sections that intersect this widget.
    int firstVisual = -1;
    int lastVisual = -1;
    /// Slot window of a pane that packs its own columns (see below); -1 = not applicable.
    int firstPaneSlot = -1;
    int lastPaneSlot = -1;
    if (!m_paneFilterActive) {
        // A whole-table header shares the geometry's viewport offset, so the
        // geometry can answer this with a binary search over its prefix sums
        // instead of a scan over every column.
        const VisibleRange candidates = m_geometry->visibleVisualRange(this->width());
        if (candidates.isValid()) {
            firstVisual = candidates.first;
            lastVisual = candidates.last;
        }
    } else if (m_paneOffset == kFollowGeometryOffset) {
        // A pane that follows the committed geometry reads the same mapping as the body,
        // only shifted by its own rect inside the viewport (the table's scrolling pane
        // starts to the right of the frozen columns). The window is still a binary search
        // over the geometry's prefix sums - scanning every column here was the other half
        // of the O(N^2) pane-header path (P1-8 of the second review).
        const qint64 ownOriginX = m_viewportOriginSet ? x() : 0;
        const qint64 paneStart = m_geometry->viewportOffset() + ownOriginX - m_viewportOrigin.x();
        const VisibleRange candidates = m_geometry->visibleVisualRangeFor(paneStart, this->width());
        if (candidates.isValid()) {
            firstVisual = candidates.first;
            lastVisual = candidates.last;
        }
    } else {
        // An explicitly offset pane (a frozen pane or a non-primary scroll group) packs its
        // own columns from its own left edge: the visible window is a range of *slots* in the
        // pane cache, found with two binary searches over the pane's prefix sums.
        rebuildPaneCacheIfNeeded();
        if (!m_paneOrder.isEmpty()) {
            const qint64 windowStart = m_paneOffset;
            const qint64 windowEnd = m_paneOffset + qMax(0, this->width());
            // First slot whose right edge lies past the window start.
            qsizetype firstSlot = qsizetype(std::lower_bound(m_panePrefixX.cbegin(), m_panePrefixX.cend(),
                                                             windowStart)
                                            - m_panePrefixX.cbegin())
                - 1;
            firstSlot = qBound<qsizetype>(0, firstSlot, m_paneOrder.size() - 1);
            qsizetype lastSlot = qsizetype(std::lower_bound(m_panePrefixX.cbegin(), m_panePrefixX.cend(),
                                                            windowEnd)
                                           - m_panePrefixX.cbegin())
                - 1;
            lastSlot = qBound<qsizetype>(firstSlot, lastSlot, m_paneOrder.size() - 1);
            firstPaneSlot = int(firstSlot);
            lastPaneSlot = int(lastSlot);
        }
    }

    // 2) Wanted sections: the visible ones widened by the overscan (§19), plus
    //    every pinned section wherever it is (§36).
    m_materializationVisits = 0;
    QSet<int> wanted;
    if (firstPaneSlot >= 0) {
        // A pane with its own offset packs its own columns, and they may be spread over the
        // whole visual order: walking the pane's *slots* keeps the pass bounded by the pane
        // window instead of by the global visual range (P2 of the third review - a pane of
        // {0, 50000, 99999} used to scan 100,000 sections per relayout).
        const int from = qMax(0, firstPaneSlot - m_overscan);
        const int to = qMin(int(m_paneOrder.size()) - 1, lastPaneSlot + m_overscan);
        m_materializationVisits += to - from + 1;
        for (int slot = from; slot <= to; ++slot)
            wanted.insert(m_paneOrder.at(slot));
    } else if (firstVisual >= 0) {
        const int from = qMax(0, firstVisual - m_overscan);
        const int to = qMin(count - 1, lastVisual + m_overscan);
        m_materializationVisits += to - from + 1;
        for (int visual = from; visual <= to; ++visual) {
            const int logical = m_geometry->logicalIndex(visual);
            if (logical >= 0 && !m_geometry->isSectionHidden(logical) && !isFiltered(logical))
                wanted.insert(logical);
        }
    }
    for (auto it = m_sectionWidgets.constBegin(); it != m_sectionWidgets.constEnd(); ++it) {
        if (isSectionPinned(it.key()))
            wanted.insert(it.key());
    }

    // 3) Recycle what is no longer wanted (unbind before it enters the pool).
    for (auto it = m_sectionWidgets.begin(); it != m_sectionWidgets.end();) {
        if (wanted.contains(it.key())) {
            ++it;
            continue;
        }
        m_adapter->unbindSection(it.value(), it.key());
        it.value()->hide();
        m_recycler->recycle(m_adapter->sectionType(it.key()), it.value());
        it = m_sectionWidgets.erase(it);
    }

    // 4) Acquire and bind.
    for (int logical : wanted) {
        QWidget *widget = m_sectionWidgets.value(logical, nullptr);
        if (!widget) {
            widget = m_recycler->acquire(m_adapter->sectionType(logical));
            if (!widget)
                continue;
            widget->setParent(this);
            widget->hide();
            m_sectionWidgets.insert(logical, widget);
            m_adapter->bindSection(widget, logical);
            // Watched at bind time and then kept up to date through ChildAdded, since
            // the business may add its widgets later (§25 cursor).
            watchMouse(widget);
        }
    }

    // 5) Position: the committed geometry, or the visual geometry while a section
    //    move is in flight (§23).
    if (animateMove && m_animationEnabled && m_animationDuration > 0
        && !m_sectionWidgets.isEmpty()) {
        animateSectionMove();
    } else {
        m_slideFrom.clear();
        m_slideProgress = 1.0;
        positionSections();
    }
    update();
}

QVector<int> VirtualHeaderView::visualOrder() const
{
    QVector<int> order;
    if (!m_geometry)
        return order;
    const int count = qMin(m_geometry->sectionCount(),
                           m_labelModel ? m_labelModel->columnCount() : m_geometry->sectionCount());
    order.reserve(count);
    for (int visual = 0; visual < count; ++visual) {
        const int logical = m_geometry->logicalIndex(visual);
        if (logical < 0 || m_geometry->isSectionHidden(logical) || isFiltered(logical))
            continue;
        order.append(logical);
    }
    return order;
}

void VirtualHeaderView::positionSections()
{
    if (!m_geometry)
        return;
    if (m_dragging) {
        positionDraggedSections();
        return;
    }
    for (auto it = m_sectionWidgets.constBegin(); it != m_sectionWidgets.constEnd(); ++it) {
        const int logical = it.key();
        QWidget *widget = it.value();
        const int committed = sectionX(logical);
        if (committed == kSectionNotShown) {
            widget->hide();
            continue;
        }
        const int width = m_geometry->sectionSize(logical);
        int visual = committed;
        if (m_slideProgress < 1.0) {
            const auto from = m_slideFrom.constFind(logical);
            if (from != m_slideFrom.constEnd())
                visual = from.value() + qRound(qreal(committed - from.value()) * m_slideProgress);
        }
        const bool visible = width > 0 && visual < this->width() && visual + width > 0;
        if (!visible && !isSectionPinned(logical)) {
            widget->hide();
            continue;
        }
        widget->setGeometry(visual, 0, width, height());
        widget->show();
    }
}

int VirtualHeaderView::dragTargetIndex() const
{
    if (m_dragSection < 0 || !m_geometry)
        return -1;
    const QVector<int> shown = visualOrder();
    const int from = shown.indexOf(m_dragSection);
    if (from < 0)
        return -1;

    // The dragged section's centre decides: every other section whose centre lies left
    // of it ends up before it, which is exactly the `to` index moveSection() expects.
    const int width = m_geometry->sectionSize(m_dragSection);
    const int centre = sectionX(m_dragSection) + (m_dragCurrentX - m_dragStartX) + width / 2;
    int to = 0;
    for (int packed = 0; packed < shown.size(); ++packed) {
        if (packed == from)
            continue;
        const int otherCentre =
            sectionX(shown.at(packed)) + m_geometry->sectionSize(shown.at(packed)) / 2;
        if (otherCentre < centre)
            ++to;
    }
    return qBound(0, to, shown.size() - 1);
}

void VirtualHeaderView::positionDraggedSections()
{
    if (!m_geometry || m_dragSection < 0)
        return;
    const QVector<int> shown = visualOrder();
    const int from = shown.indexOf(m_dragSection);
    if (from < 0)
        return;
    const int to = dragTargetIndex();
    const int draggedWidth = m_geometry->sectionSize(m_dragSection);
    if (to != m_previewSlot)
        restartDragPreviewTween(to);

    for (auto it = m_sectionWidgets.constBegin(); it != m_sectionWidgets.constEnd(); ++it) {
        const int logical = it.key();
        QWidget *widget = it.value();
        const int committed = sectionX(logical);
        if (committed == kSectionNotShown) {
            widget->hide();
            continue;
        }
        const int width = m_geometry->sectionSize(logical);
        int target = committed;
        if (logical == m_dragSection) {
            // The picked up section follows the pointer, keeping the grab offset.
            target = committed + (m_dragCurrentX - m_dragStartX);
        } else {
            const int packed = shown.indexOf(logical);
            if (packed >= 0 && to >= 0) {
                if (from < to && packed > from && packed <= to)
                    target = committed - draggedWidth; // the gap closes behind it
                else if (from > to && packed >= to && packed < from)
                    target = committed + draggedWidth; // the gap opens in front of it
            }
        }
        // The dragged section tracks the pointer exactly; the others tween towards their
        // slot, so making room reads as a movement instead of a jump (§23).
        int visual = target;
        if (logical != m_dragSection && m_previewProgress < 1.0) {
            const auto start = m_previewFrom.constFind(logical);
            if (start != m_previewFrom.constEnd())
                visual = start.value()
                    + int(qRound(qreal(target - start.value()) * m_previewProgress));
        }
        const bool visible = width > 0 && visual < this->width() && visual + width > 0;
        if (!visible && !isSectionPinned(logical)) {
            widget->hide();
            continue;
        }
        widget->setGeometry(visual, 0, width, height());
        widget->show();
    }
}

void VirtualHeaderView::restartDragPreviewTween(int packedSlot)
{
    m_previewSlot = packedSlot;
    m_previewFrom.clear();
    for (auto it = m_sectionWidgets.constBegin(); it != m_sectionWidgets.constEnd(); ++it) {
        if (it.value()->isVisible())
            m_previewFrom.insert(it.key(), it.value()->x());
    }
    if (!m_previewAnimation || !m_animationEnabled || m_animationDuration <= 0) {
        m_previewProgress = 1.0;
        m_previewFrom.clear();
        return;
    }
    m_previewAnimation->stop();
    m_previewAnimation->setDuration(m_animationDuration);
    m_previewProgress = 0.0;
    m_previewAnimation->start();
}

void VirtualHeaderView::beginSectionDrag(int logicalIndex, int x)
{
    // A running transition would fight the preview.
    if (m_slideAnimation)
        m_slideAnimation->stop();
    m_slideFrom.clear();
    m_slideProgress = 1.0;

    m_dragSection = logicalIndex;
    m_dragStartX = x;
    m_dragCurrentX = x;
    m_dragging = true;
    m_moved = true; // a drag is never a sort click
    // Nothing to tween yet: the sections still sit on their committed positions, and the
    // tween starts as soon as the pointer asks for a different insertion slot.
    m_previewSlot = visualOrder().indexOf(logicalIndex);
    m_previewFrom.clear();
    m_previewProgress = 1.0;
    setCursor(Qt::ClosedHandCursor);
    positionSections();
}

void VirtualHeaderView::updateSectionDrag(int x)
{
    if (!m_dragging || x == m_dragCurrentX)
        return;
    m_dragCurrentX = x;
    positionSections();
}

void VirtualHeaderView::finishSectionDrag(bool commit)
{
    const int dragged = m_dragSection;
    const bool wasDragging = m_dragging;
    int fromVisual = -1;
    int toVisual = -1;
    if (wasDragging && dragged >= 0 && m_geometry) {
        // The drag works in the packed order of the sections this header shows (hidden
        // and filtered columns are not part of it), while moveSection() takes visual
        // indices - so the target is converted by asking where the section that should
        // precede the dragged one currently sits.
        const QVector<int> shown = visualOrder();
        const int packedFrom = shown.indexOf(dragged);
        const int packedTo = dragTargetIndex();
        if (packedFrom >= 0 && packedTo >= 0 && packedTo != packedFrom) {
            QVector<int> rest = shown;
            rest.removeAt(packedFrom);
            if (!rest.isEmpty()) {
                fromVisual = m_geometry->visualIndex(dragged);
                if (packedTo > 0) {
                    const int anchor =
                        m_geometry->visualIndex(rest.at(qMin(packedTo - 1, rest.size() - 1)));
                    toVisual = anchor - (fromVisual < anchor ? 1 : 0) + 1;
                } else {
                    const int first = m_geometry->visualIndex(rest.first());
                    toVisual = first - (fromVisual < first ? 1 : 0);
                }
            }
        }
    }

    m_dragSection = -1;
    m_dragging = false;
    if (m_previewAnimation)
        m_previewAnimation->stop();
    m_previewFrom.clear();
    m_previewProgress = 1.0;
    m_previewSlot = -1;
    updateCursor(mapFromGlobal(QCursor::pos()));

    if (commit && fromVisual >= 0 && toVisual >= 0) {
        // §23: one commit, then the transition settles from where the preview left the
        // sections. The body relayouts once, on this commit.
        setSectionMoveAnimated(true);
        m_geometry->moveSection(fromVisual, toVisual);
        return; // the relayout triggered by the commit positions everything
    }
    // Cancelled or a no-op drag: back to the committed geometry.
    m_slideFrom.clear();
    m_slideProgress = 1.0;
    positionSections();
    update();
}

void VirtualHeaderView::animateSectionMove()
{
    // Where the materialized sections are right now: an interrupted transition
    // continues from the current visual position, never from a stale one.
    m_slideFrom.clear();
    for (auto it = m_sectionWidgets.constBegin(); it != m_sectionWidgets.constEnd(); ++it) {
        if (it.value()->isVisible())
            m_slideFrom.insert(it.key(), it.value()->x());
    }

    if (!m_slideAnimation) {
        m_slideAnimation = new QVariantAnimation(this);
        m_slideAnimation->setStartValue(0.0);
        m_slideAnimation->setEndValue(1.0);
        m_slideAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_slideAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    m_slideProgress = value.toReal();
                    positionSections();
                });
        connect(m_slideAnimation, &QVariantAnimation::finished, this, [this]() {
            if (m_slideProgress < 1.0)
                return; // stopped before the end, not finished
            m_slideProgress = 1.0;
            m_slideFrom.clear();
            positionSections();
        });
    }

    m_slideAnimation->stop();
    m_slideAnimation->setDuration(m_animationDuration);
    m_slideProgress = 0.0;
    positionSections();
    m_slideAnimation->start();
}

bool VirtualHeaderView::isSectionPinned(int logicalIndex) const
{
    // §36: never recycle a section the user is interacting with.
    const QWidget *widget = m_sectionWidgets.value(logicalIndex, nullptr);
    if (!widget)
        return false;
    if (const QWidget *focus = QApplication::focusWidget()) {
        if (focus == widget || widget->isAncestorOf(focus))
            return true;
    }
    const QWidget *popup = QApplication::activePopupWidget();
    if (popup && (popup == widget || widget->isAncestorOf(popup) || popup->isAncestorOf(widget)))
        return true;
    return false;
}

void VirtualHeaderView::recycleAllSections()
{
    if (!m_adapter) {
        m_sectionWidgets.clear();
        return;
    }
    for (auto it = m_sectionWidgets.begin(); it != m_sectionWidgets.end(); ++it) {
        m_adapter->unbindSection(it.value(), it.key());
        it.value()->hide();
        m_recycler->recycle(m_adapter->sectionType(it.key()), it.value());
    }
    m_sectionWidgets.clear();
}

QList<int> VirtualHeaderView::materializedSections() const
{
    QList<int> sections = m_sectionWidgets.keys();
    std::sort(sections.begin(), sections.end());
    return sections;
}

QWidget *VirtualHeaderView::sectionWidget(int logicalIndex) const
{
    return m_sectionWidgets.value(logicalIndex, nullptr);
}

qsizetype VirtualHeaderView::pooledSectionCount() const
{
    return m_recycler ? m_recycler->pooledCount() : 0;
}

// ---------------------------------------------------------------------------
// Interaction (§21/§22/§25)
// ---------------------------------------------------------------------------

int VirtualHeaderView::sectionAt(const QPoint &pos) const
{
    if (!m_geometry)
        return -1;
    for (int logical : materializedSections()) {
        const int left = sectionX(logical);
        if (left == kSectionNotShown)
            continue;
        if (pos.x() >= left && pos.x() < left + m_geometry->sectionSize(logical))
            return logical;
    }
    return -1;
}

int VirtualHeaderView::resizeEdgeAt(const QPoint &pos) const
{
    if (!m_geometry)
        return -1;
    for (int logical : materializedSections()) {
        const int left = sectionX(logical);
        if (left == kSectionNotShown)
            continue;
        const int right = left + m_geometry->sectionSize(logical);
        if (qAbs(pos.x() - right) <= kResizeMargin)
            return logical;
        if (qAbs(pos.x() - left) <= kResizeMargin) {
            // The leading edge belongs to the previous section.
            const int visual = m_geometry->visualIndex(logical);
            if (visual > 0)
                return m_geometry->logicalIndex(visual - 1);
        }
    }
    return -1;
}

void VirtualHeaderView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_geometry) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPoint pos = eventPosition(event);
    m_moved = false;
    m_dragSection = -1;
    m_dragging = false;
    m_resizeSection = resizeEdgeAt(pos);
    if (m_resizeSection >= 0) {
        m_resizeStartSize = m_geometry->storedSectionSize(m_resizeSection);
        m_resizeStartX = pos.x();
        m_pressedSection = -1;
        setCursor(Qt::SplitHCursor); // the gesture owns the cursor until the release
        event->accept();
        return;
    }
    m_pressedSection = sectionAt(pos);
    m_pressedX = pos.x();
    event->accept();
}

void VirtualHeaderView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_geometry) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint pos = eventPosition(event);
    if (m_resizeSection >= 0) {
        const int width = qMax(m_geometry->minimumSectionSize(),
                               m_resizeStartSize + pos.x() - m_resizeStartX);
        m_geometry->resizeSection(m_resizeSection, width);
        event->accept();
        return;
    }
    if (m_pressedSection >= 0 && (event->buttons() & Qt::LeftButton)) {
        // §22/§23: a drag is one gesture with one commit. Until the pointer passes the
        // drag distance it is still a click; after that the sections only move in the
        // *visual* geometry (a preview), and the committed order changes on the release.
        if (!m_dragging) {
            if (qAbs(pos.x() - m_pressedX) < QApplication::startDragDistance()) {
                event->accept();
                return;
            }
            beginSectionDrag(m_pressedSection, m_pressedX);
        }
        updateSectionDrag(pos.x());
        event->accept();
        return;
    }
    updateCursor(pos);
    QWidget::mouseMoveEvent(event);
}

void VirtualHeaderView::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_geometry) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const QPoint pos = eventPosition(event);
    const bool wasResize = m_resizeSection >= 0;
    const bool wasDragging = m_dragging;
    const int pressed = m_pressedSection;
    if (wasDragging) {
        finishSectionDrag(true);
        m_resizeSection = -1;
        m_pressedSection = -1;
        m_moved = false;
        event->accept();
        return;
    }
    m_resizeSection = -1;
    m_pressedSection = -1;
    updateCursor(pos);

    // A plain click on a section sets the sort indicator (§33).
    if (!wasResize && !m_moved && pressed >= 0 && sectionAt(pos) == pressed
        && m_sortInteractionEnabled) {
        const bool same = m_geometry->sortIndicatorSection() == pressed;
        const Qt::SortOrder order = same && m_geometry->sortIndicatorOrder() == Qt::AscendingOrder
            ? Qt::DescendingOrder
            : Qt::AscendingOrder;
        m_geometry->setSortIndicator(pressed, order);
    }
    m_moved = false;
    event->accept();
}

void VirtualHeaderView::updateCursor(const QPoint &pos)
{
    if (m_dragging || m_resizeSection >= 0)
        return; // the gesture owns the cursor
    if (!rect().contains(pos)) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    setCursor(resizeEdgeAt(pos) >= 0 ? Qt::SplitHCursor : Qt::ArrowCursor);
}

void VirtualHeaderView::watchMouse(QWidget *root)
{
    // The section widgets cover the header, so the header itself sees almost no mouse
    // moves: every section widget - and everything the business put inside it - has to
    // report its position back (§25). Without this the resize cursor would stick to
    // whatever it was set to last, for the whole header, because children inherit the
    // parent's cursor.
    if (!root)
        return;
    root->setMouseTracking(true);
    root->installEventFilter(this);
    const QList<QWidget *> children = root->findChildren<QWidget *>();
    for (QWidget *child : children) {
        child->setMouseTracking(true);
        child->installEventFilter(this);
    }
}

bool VirtualHeaderView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded) {
        // A widget the business creates after the section was bound - a state label
        // that only appears when there is something to report, a button that is
        // added on demand - is not in the scan done at bind time. Its parent reports
        // its arrival here, and the new subtree is watched recursively (§25).
        auto *child = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child());
        if (child)
            watchMouse(child);
        return QWidget::eventFilter(watched, event);
    }
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || !m_geometry)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseMove: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        updateCursor(widget->mapTo(this, eventPosition(mouse)));
        break;
    }
    case QEvent::Enter:
    case QEvent::Leave:
        // Enter/Leave carry no usable position, and the pointer may move from one child
        // to another without the header seeing a move: ask the pointer itself.
        updateCursor(mapFromGlobal(QCursor::pos()));
        break;
    default:
        break;
    }
    return false; // never consume: the business widget keeps its own events
}

void VirtualHeaderView::leaveEvent(QEvent *event)
{
    unsetCursor();
    QWidget::leaveEvent(event);
}

void VirtualHeaderView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_dragging) {
        // Escape drops the preview: no commit, the sections go back to where the
        // committed geometry says they are.
        finishSectionDrag(false);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void VirtualHeaderView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

} // namespace viv
