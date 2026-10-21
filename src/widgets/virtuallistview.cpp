#include <virtualitemviews/virtuallistview.h>
#include "../core/pixelalignedlines_p.h"
#include "../core/rowdragpreview_p.h"

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/listlayout.h>

#include <QAbstractItemModel>
#include <QLoggingCategory>
#include <QPalette>
#include <QPainter>
#include <QPaintEvent>
#include <QScopeGuard>
#include <QVariantAnimation>

namespace viv {

VirtualListView::VirtualListView(QWidget *parent)
    : VirtualItemView(parent), m_dragRowPreview(new RowDragPreview)
{
    m_listLayout = new ListLayout(Qt::Vertical);
    setLayoutPolicy(m_listLayout, true);
    m_dragReorderAnimation = new QVariantAnimation(this);
    m_dragReorderAnimation->setStartValue(0.0);
    m_dragReorderAnimation->setEndValue(1.0);
    connect(m_dragReorderAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &) { applyDragReorderFrame(); });
    connect(m_dragReorderAnimation, &QVariantAnimation::finished, this, [this]() {
        m_dragReorderClock.invalidate();
        applyDragReorderFrame();
    });
}

VirtualListView::~VirtualListView()
{
    m_dragReorderAnimation->stop();
    m_dragRowPreview->clear();
    for (const QMetaObject::Connection &connection : m_dragModelConnections)
        disconnect(connection);
    qDeleteAll(m_rowGridLines);
    qDeleteAll(m_rowGridLinePool);
}

void VirtualListView::setRowGridLinesVisible(bool visible)
{
    if (m_rowGridLinesVisible == visible)
        return;
    m_rowGridLinesVisible = visible;
    relayout();
}

void VirtualListView::setVisualStateBackgroundVisible(bool visible)
{
    if (m_visualStateBackgroundVisible == visible)
        return;
    m_visualStateBackgroundVisible = visible;
    viewport()->update();
}

void VirtualListView::setVisualStateCornerRadius(int radius)
{
    radius = qMax(0, radius);
    if (m_visualStateCornerRadius == radius)
        return;
    m_visualStateCornerRadius = radius;
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

void VirtualListView::refreshVisualStates()
{
    VirtualItemView::refreshVisualStates();
    viewport()->update();
}

void VirtualListView::refreshVisualState(const QModelIndex &index)
{
    VirtualItemView::refreshVisualState(index);
    if (m_visualStateBackgroundVisible)
        viewport()->update();
}

void VirtualListView::paintEvent(QPaintEvent *event)
{
    VirtualItemView::paintEvent(event);
    if (!m_visualStateBackgroundVisible)
        return;
    QPainter painter(viewport());
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0)
            continue;
        const QRect rect = visualRowRect(row);
        const QRegion clip(rect.intersected(itemPaneRect(itemPaneForRow(row)))
            .intersected(viewport()->rect()).intersected(event->rect()));
        if (clip.isEmpty())
            continue;
        const VisualState state = visualState(item.index);
        painter.setOpacity(state.hoverProgress);
        fillPixelAlignedRoundedRegion(&painter, rect, clip, m_visualStateCornerRadius,
                                      hoverBackgroundColor());
        painter.setOpacity(state.selectedProgress);
        fillPixelAlignedRoundedRegion(&painter, rect, clip, m_visualStateCornerRadius,
                                      selectedBackgroundColor());
    }
}

void VirtualListView::setRowGridLineWidth(int pixels)
{
    const int width = qMax(1, pixels);
    if (m_rowGridLineWidth == width)
        return;
    m_rowGridLineWidth = width;
    relayout();
}

void VirtualListView::setRowGridLineColor(const QColor &color)
{
    if (m_rowGridLineColor == color)
        return;
    m_rowGridLineColor = color;
    relayout();
}

QColor VirtualListView::itemPaneSeparatorColor() const
{
    return m_rowGridLineColor.isValid() ? m_rowGridLineColor
                                        : VirtualItemView::itemPaneSeparatorColor();
}

void VirtualListView::configureRowSpacingWidget(QWidget *widget) const
{
    widget->setProperty("vivShowSpacingLines", m_rowGridLinesVisible);
    widget->setProperty("vivSpacingLineWidth", m_rowGridLineWidth);
}

void VirtualListView::afterMaterialize()
{
    const QPointer<VirtualListView> self(this);
    VirtualItemView::afterMaterialize();
    if (!self)
        return;
    if (m_dragRowPreview->isActive()) {
        applyDragReorderFrame();
        return;
    }
    syncRowGridLines();
}

void VirtualListView::syncRowGridLines()
{
    QHash<qsizetype, QRect> desired;
    const QRect viewportRect = viewport()->geometry();
    if (m_rowGridLinesVisible && rowSpacing() == 0 && viewportRect.width() > 0) {
        for (const VisibleRange &range : visibleItemRanges()) {
            for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
                if (row + 1 >= viewItemCount()
                    && (!m_dragRowPreview->isActive() || qFuzzyIsNull(m_dragRowPreview->offset(row, 1))))
                    continue;
                const QRect rowRect = visualRowRect(row);
                if (!rowRect.intersects(viewport()->rect()))
                    continue;
                const QRect lineRect(viewportRect.x(),
                                     viewportRect.y() + rowRect.bottom() - m_rowGridLineWidth + 1,
                                     viewportRect.width(), m_rowGridLineWidth);
                const QRect paneRect = itemPaneRect(itemPaneForRow(row))
                    .translated(viewportRect.topLeft());
                const QRect clipped = lineRect.intersected(paneRect);
                if (!clipped.isEmpty() && !isRowGridLineCoveredByPaneSeparator(clipped))
                    desired.insert(row, clipped);
            }
        }
    }
    for (auto it = m_rowGridLines.begin(); it != m_rowGridLines.end();) {
        if (desired.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->hide();
        m_rowGridLinePool.append(it.value());
        it = m_rowGridLines.erase(it);
    }
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        QWidget *line = m_rowGridLines.value(it.key(), nullptr);
        if (!line) {
            line = m_rowGridLinePool.isEmpty() ? new PixelAlignedHorizontalLine(this) : m_rowGridLinePool.takeLast();
            line->setObjectName(QStringLiteral("vivListRowGridLine"));
            line->setAttribute(Qt::WA_TransparentForMouseEvents);
            line->setAutoFillBackground(false);
            m_rowGridLines.insert(it.key(), line);
        }
        QPalette colors = line->palette();
        colors.setColor(QPalette::Window, itemPaneSeparatorColor());
        line->setPalette(colors);
        line->setGeometry(it.value());
        line->show();
        line->raise();
    }
}

// ---------------------------------------------------------------------------
// Native row-drag presentation
// ---------------------------------------------------------------------------

void VirtualListView::setDragFeedbackMode(DragFeedbackMode mode)
{
    if (m_dragFeedbackMode == mode)
        return;
    m_dragFeedbackMode = mode;
    const QPointer<VirtualListView> self(this);
    clearDragFeedback();
    if (self)
        refreshDragFeedback();
}

void VirtualListView::setDragReorderAnimationDuration(int milliseconds)
{
    milliseconds = qMax(0, milliseconds);
    if (m_dragReorderAnimationDuration == milliseconds)
        return;
    m_dragReorderAnimationDuration = milliseconds;
    m_dragReorderAnimation->stop();
    m_dragReorderClock.invalidate();
    if (m_dragRowPreview->isActive())
        applyDragReorderFrame();
}

void VirtualListView::setDragReorderAnimationEasingCurve(const QEasingCurve &curve)
{
    if (m_dragReorderEasingCurve == curve)
        return;
    m_dragReorderEasingCurve = curve;
    m_dragReorderAnimation->stop();
    m_dragReorderClock.invalidate();
    if (m_dragRowPreview->isActive())
        applyDragReorderFrame();
}

qreal VirtualListView::dragReorderProgress() const
{
    if (!m_dragReorderClock.isValid() || m_dragReorderAnimation->state() != QAbstractAnimation::Running
        || m_dragReorderAnimationDuration <= 0)
        return 1.0;
    const qint64 elapsed = qMax<qint64>(m_dragReorderClock.elapsed(), m_dragReorderAnimation->currentTime());
    return m_dragReorderEasingCurve.valueForProgress(qBound<qreal>(0,
        qreal(elapsed) / m_dragReorderAnimationDuration, 1));
}

QRect VirtualListView::visualRowRect(qsizetype row) const
{
    QRect rect = geometryForViewRow(row);
    if (m_dragRowPreview->isActive() && !isRowFrozen(row)) {
        if (row == m_dragRowPreview->source())
            rect.moveTop(-rect.height() - rowSpacing() - 1);
        else
            rect.translate(0, qRound(m_dragRowPreview->offset(row, dragReorderProgress())));
    }
    return rect;
}

void VirtualListView::applyDragReorderFrame()
{
    if (m_dragReorderFrameActive)
        return;
    const QPointer<VirtualListView> self(this);
    const QPointer<QAbstractItemModel> activeModel(model());
    const quint64 modelSerial = modelChangeSerial();
    const quint64 previewSerial = m_dragPreviewSerial;
    const QPersistentModelIndex root = m_rootIndex;
    const auto isCurrent = [=]() {
        return self && activeModel && self->model() == activeModel.data()
            && self->modelChangeSerial() == modelSerial && self->m_rootIndex == root
            && self->m_dragPreviewSerial == previewSerial;
    };
    if (!isCurrent())
        return;
    m_dragReorderFrameActive = true;
    const auto cleanup = qScopeGuard([self]() {
        if (self)
            self->m_dragReorderFrameActive = false;
    });
    const auto items = materializedItems();
    QVector<QPointer<QWidget>> widgets;
    widgets.reserve(items.size());
    for (const MaterializedItem &item : items)
        widgets.append(item.widget);
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (!isCurrent())
            return;
        const MaterializedItem &item = items.at(i);
        const QPointer<QWidget> widget = widgets.at(i);
        if (!widget || !item.index.isValid() || widgetForIndex(item.index) != widget.data())
            continue;
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0 || isRowFrozen(row))
            continue;
        QWidget *parent = widget->parentWidget();
        const int parentY = parent && parent != viewport() ? parent->y() : 0;
        widget->move(widget->x(), visualRowRect(row).y() - parentY);
    }
    if (!isCurrent())
        return;
    updateRowSpacingVisualGeometry([this](qsizetype row) {
        return visualRowRect(row).y() - geometryForViewRow(row).y();
    });
    if (!isCurrent())
        return;
    syncRowGridLines();
    if (!isCurrent())
        return;
    viewport()->update();
    raiseItemPaneSeparatorLines();
}

bool VirtualListView::updateDragFeedback(const DropTarget &target, const QPoint &position,
                                        Qt::DropAction action,
                                        const QList<QPersistentModelIndex> &sources)
{
    const auto useIndicator = [this]() {
        clearDragFeedback();
        return false;
    };
    if (m_dragFeedbackMode != DragFeedbackMode::RowMoveAnimation || action != Qt::MoveAction
        || sources.isEmpty() || !model() || !target.isValid() || target.parent != m_rootIndex
        || target.ontoItem || target.cellDrop || target.column >= 0
        || !viewport()->rect().contains(position)
        || itemPaneAtY(position.y()) != ItemPane::Type::Scrollable)
        return useIndicator();
    const QPersistentModelIndex source = sources.first();
    for (const QPersistentModelIndex &index : sources) {
        if (!index.isValid() || index.model() != model() || index.parent() != m_rootIndex
            || index.row() != source.row())
            return useIndicator();
    }
    const qsizetype sourceRow = viewItemForIndex(source);
    if (sourceRow < 0 || isRowFrozen(sourceRow) || target.row < frozenRows()
        || target.row > viewItemCount() - frozenBottomRows())
        return useIndicator();
    const qreal extent = qreal(geometryForViewRow(sourceRow).height()) + rowSpacing();
    if (m_dragRowPreview->isActive() && m_dragPreviewSource == source
        && m_dragRowPreview->destination() == target.row && m_dragRowPreview->extent() == extent) {
        applyDragReorderFrame();
        return true;
    }
    QVector<qsizetype> rows;
    for (const VisibleRange &range : visibleItemRanges()) {
        for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row)
            rows.append(row);
    }
    const qreal progress = dragReorderProgress();
    m_dragReorderAnimation->stop();
    m_dragRowPreview->retarget(sourceRow, target.row, extent, rows, progress);
    ++m_dragPreviewSerial;
    m_dragPreviewSource = source;
    m_dragReorderClock.invalidate();
    if (m_dragReorderAnimationDuration > 0) {
        m_dragReorderAnimation->setDuration(m_dragReorderAnimationDuration);
        m_dragReorderAnimation->setEasingCurve(m_dragReorderEasingCurve);
        m_dragReorderClock.start();
        const QPointer<VirtualListView> self(this);
        m_dragReorderAnimation->start();
        if (!self)
            return true;
    }
    applyDragReorderFrame();
    return true;
}

void VirtualListView::clearDragFeedback()
{
    if (!m_dragRowPreview->isActive())
        return;
    m_dragReorderAnimation->stop();
    m_dragReorderClock.invalidate();
    m_dragRowPreview->clear();
    ++m_dragPreviewSerial;
    m_dragPreviewSource = QPersistentModelIndex();
    if (m_dragReorderFrameActive) {
        markDirty();
        return;
    }
    applyDragReorderFrame();
}

// ---------------------------------------------------------------------------
// Identity mapping
// ---------------------------------------------------------------------------

qsizetype VirtualListView::viewItemCount() const
{
    QAbstractItemModel *m = model();
    return m ? qMax<qsizetype>(0, m->rowCount(m_rootIndex)) : 0;
}

QModelIndex VirtualListView::viewIndex(qsizetype item, int column) const
{
    QAbstractItemModel *m = model();
    if (!m || item < 0 || item >= qsizetype(m->rowCount(m_rootIndex)))
        return QModelIndex();
    return m->index(int(item), column, m_rootIndex);
}

qsizetype VirtualListView::viewItemForIndex(const QModelIndex &index) const
{
    if (!index.isValid() || !model())
        return -1;
    if (index.parent() != m_rootIndex)
        return -1;
    if (index.row() < 0 || index.row() >= model()->rowCount(m_rootIndex))
        return -1;
    return index.row();
}

bool VirtualListView::isLayoutParent(const QModelIndex &parent) const
{
    return parent == m_rootIndex;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void VirtualListView::setModel(QAbstractItemModel *model)
{
    const QPointer<VirtualListView> self(this);
    const QPointer<QAbstractItemModel> requestedModel(model);
    clearDragFeedback();
    if (!self)
        return;
    for (const QMetaObject::Connection &connection : m_dragModelConnections)
        disconnect(connection);
    m_dragModelConnections.clear();
    // The root names an item of the previous model; keeping it would hand a
    // foreign parent to the new model's rowCount().
    m_rootIndex = QModelIndex();
    VirtualItemView::setModel(requestedModel.data());
    if (!self || !requestedModel || this->model() != requestedModel.data())
        return;
    // Numeric preview offsets must be discarded before row identities change.
    const auto discardPreview = [this]() {
        ++m_dragPreviewSerial;
        clearDragFeedback();
    };
    m_dragModelConnections = {
        connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, discardPreview),
        connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, discardPreview),
        connect(model, &QAbstractItemModel::rowsAboutToBeMoved, this, discardPreview),
        connect(model, &QAbstractItemModel::columnsAboutToBeInserted, this, discardPreview),
        connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, this, discardPreview),
        connect(model, &QAbstractItemModel::columnsAboutToBeMoved, this, discardPreview),
        connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this, discardPreview),
        connect(model, &QAbstractItemModel::modelAboutToBeReset, this, discardPreview),
        connect(model, &QObject::destroyed, this, discardPreview)
    };
}

void VirtualListView::setRootIndex(const QModelIndex &index)
{
    if (index == m_rootIndex)
        return;
    if (index.isValid() && index.model() != model()) {
        qWarning("VirtualListView::setRootIndex(): the index belongs to another model "
                 "(or the view has no model); the root is unchanged");
        return;
    }

    const QPointer<VirtualListView> self(this);
    const bool hadRoot = index.isValid();
    const QPersistentModelIndex requestedRoot(index);
    const quint64 modelSerial = modelChangeSerial();
    clearDragFeedback();
    if (!self || modelChangeSerial() != modelSerial || (hadRoot && !requestedRoot.isValid()))
        return;
    m_rootIndex = requestedRoot;
    // Every identity mapping changed: rebuild the materialized set.
    recycleAllItems();
    if (!self || modelChangeSerial() != modelSerial || m_rootIndex != requestedRoot)
        return;
    resetLayoutForNewModel();
    if (!self || modelChangeSerial() != modelSerial || m_rootIndex != requestedRoot)
        return;
    relayout();
}

} // namespace viv
