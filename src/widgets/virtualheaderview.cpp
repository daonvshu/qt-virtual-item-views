#include <virtualitemviews/virtualheaderview.h>

#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/widgetrecycler.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSet>
#include <QVariantAnimation>

#include <algorithm>

namespace viv {

namespace {
/// Pixels at a section edge that start a resize instead of a click/move (§25).
constexpr int kResizeMargin = 3;

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
    m_recycler = new WidgetRecycler(this);
    m_recycler->setParentWidget(this);
    m_recycler->setFactory([this](WidgetType type, QWidget *parent) -> QWidget * {
        return m_adapter ? m_adapter->createSection(type, parent) : nullptr;
    });
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
    connectGeometry(m_geometry, false);
    m_geometry = geometry;
    connectGeometry(m_geometry, true);
    relayout();
}

void VirtualHeaderView::connectGeometry(HeaderGeometry *geometry, bool connectSignals)
{
    if (!geometry)
        return;
    if (connectSignals) {
        // Any of these changes moves sections or changes which ones own a widget.
        connect(geometry, &HeaderGeometry::geometryChanged, this, [this]() { relayout(); });
        connect(geometry, &HeaderGeometry::sectionCountChanged, this, [this](int) { relayout(); });
        connect(geometry, &HeaderGeometry::offsetChanged, this, [this](qint64) { relayout(); });
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
        // Header labels changed: rebind the materialized sections.
        connect(m_labelModel, &QAbstractItemModel::headerDataChanged, this,
                [this](Qt::Orientation, int, int) { relayout(); });
        connect(m_labelModel, &QAbstractItemModel::modelReset, this, [this]() { relayout(); });
        connect(m_labelModel, &QAbstractItemModel::columnsInserted, this,
                [this](const QModelIndex &, int, int) { relayout(); });
        connect(m_labelModel, &QAbstractItemModel::columnsRemoved, this,
                [this](const QModelIndex &, int, int) { relayout(); });
        connect(m_labelModel, &QAbstractItemModel::columnsMoved, this,
                [this](const QModelIndex &, int, int, const QModelIndex &, int) { relayout(); });
    }
    relayout();
}

void VirtualHeaderView::setSortInteractionEnabled(bool enabled)
{
    m_sortInteractionEnabled = enabled;
}

void VirtualHeaderView::setPaneFilter(const QVector<int> &logicalColumns, bool frozen)
{
    Q_UNUSED(frozen);
    m_paneFilter = logicalColumns;
    m_paneFilterActive = true;
    relayout();
}

void VirtualHeaderView::clearPaneFilter()
{
    if (!m_paneFilterActive)
        return;
    m_paneFilterActive = false;
    m_paneFilter.clear();
    m_paneOffset = kFollowGeometryOffset;
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
    if (m_adapter == adapter)
        return;
    recycleAllSections();
    if (m_ownAdapter)
        delete m_adapter;
    m_adapter = adapter;
    m_ownAdapter = adapter && takeOwnership;
    relayout();
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
    return m_paneFilterActive && !m_paneFilter.contains(logicalIndex);
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
        // The columns of a pane are not necessarily a contiguous slice of the
        // committed order, so their widths are accumulated over the pane's list.
        int localX = 0;
        for (int column : m_paneFilter) {
            if (column == logicalIndex)
                break;
            if (m_geometry->isSectionHidden(column))
                continue;
            localX += m_geometry->sectionSize(column);
        }
        return localX - int(m_paneOffset);
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
    const QVector<int> order = visualOrder();
    bool sectionsReordered = false;
    if (!m_lastVisualOrder.isEmpty() && m_lastVisualOrder.size() == order.size()) {
        QVector<int> before = m_lastVisualOrder;
        QVector<int> after = order;
        std::sort(before.begin(), before.end());
        std::sort(after.begin(), after.end());
        sectionsReordered = before == after && m_lastVisualOrder != order;
    }
    m_lastVisualOrder = order;
    const bool animateMove = sectionsReordered && m_animateOrderChange;
    m_animateOrderChange = false; // the request is consumed by this pass

    // 1) Visual range of the sections that intersect this widget.
    int firstVisual = -1;
    int lastVisual = -1;
    for (int visual = 0; visual < count; ++visual) {
        const int logical = m_geometry->logicalIndex(visual);
        if (logical < 0 || m_geometry->isSectionHidden(logical) || isFiltered(logical))
            continue;
        const int left = sectionX(logical);
        const int width = m_geometry->sectionSize(logical);
        if (left == kSectionNotShown || width <= 0)
            continue;
        if (left < this->width() && left + width > 0) {
            if (firstVisual < 0)
                firstVisual = visual;
            lastVisual = visual;
        }
    }

    // 2) Wanted sections: the visible ones widened by the overscan (§19), plus
    //    every pinned section wherever it is (§36).
    QSet<int> wanted;
    if (firstVisual >= 0) {
        const int from = qMax(0, firstVisual - m_overscan);
        const int to = qMin(count - 1, lastVisual + m_overscan);
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
    m_resizeSection = resizeEdgeAt(pos);
    if (m_resizeSection >= 0) {
        m_resizeStartSize = m_geometry->storedSectionSize(m_resizeSection);
        m_resizeStartX = pos.x();
        m_pressedSection = -1;
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
        const int target = sectionAt(pos);
        if (target >= 0 && target != m_pressedSection) {
            // Drag a section over a neighbour: swap their visual positions (§22).
            //
            // This commits per boundary crossing and is applied immediately (no
            // animation) on purpose: §23 wants *one* commit and one transition at the
            // end of the gesture, which is what the drag rewrite will do - animating
            // every intermediate step here would only make the section lag behind the
            // cursor.
            const int from = m_geometry->visualIndex(m_pressedSection);
            const int to = m_geometry->visualIndex(target);
            if (from >= 0 && to >= 0) {
                m_geometry->moveSection(from, to);
                m_moved = true;
            }
        }
        event->accept();
        return;
    }
    setCursor(resizeEdgeAt(pos) >= 0 ? Qt::SplitHCursor : Qt::ArrowCursor);
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
    const int pressed = m_pressedSection;
    m_resizeSection = -1;
    m_pressedSection = -1;
    unsetCursor();

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

void VirtualHeaderView::leaveEvent(QEvent *event)
{
    unsetCursor();
    QWidget::leaveEvent(event);
}

void VirtualHeaderView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

} // namespace viv
