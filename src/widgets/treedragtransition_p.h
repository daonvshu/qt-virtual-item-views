#pragma once

#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/listlayout.h>
#include "../core/rowdragpreview_p.h"
#include "../core/performanceprobe_p.h"
#include "treedragplaceholder_p.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QRegion>
#include <QScopeGuard>
#include <QVariantAnimation>
#include <algorithm>
#include <limits>

namespace viv {

// A visual-only insertion preview over flattened tree rows. Model identities
// and natural hit coordinates remain unchanged until the model accepts a drop.
class TreeDragTransition
{
public:
    explicit TreeDragTransition(VirtualTreeView *view) : m_view(view)
    {
        m_animation = new QVariantAnimation(view);
        m_animation->setStartValue(0.0);
        m_animation->setEndValue(1.0);
        QObject::connect(m_animation, &QVariantAnimation::valueChanged, view,
            [this](const QVariant &) { applyFrame(true); });
        QObject::connect(m_animation, &QVariantAnimation::finished, view, [this]() {
            m_clock.invalidate();
            applyFrame(true);
        });
    }

    ~TreeDragTransition()
    {
        delete m_outline;
        delete m_animation;
    }

    VirtualTreeView::DragFeedbackMode mode = VirtualTreeView::DragFeedbackMode::RowMoveAnimation;
    int duration = 180;
    QEasingCurve easing = QEasingCurve::OutCubic;

    bool isActive() const { return m_preview.isActive(); }

    void finishTween()
    {
        m_animation->stop();
        m_clock.invalidate();
        if (isActive())
            applyFrame(true);
    }

    void clear()
    {
        ++m_revision;
        if (!isActive())
            return;
        m_animation->stop();
        m_clock.invalidate();
        m_preview.clear();
        m_sampledRows.clear();
        m_source = QPersistentModelIndex();
        m_targetParent = QPersistentModelIndex();
        m_targetRow = -1;
        m_outlineAmbiguous = false;
        const QPointer<VirtualTreeView> guard(m_view);
        if (m_outline)
            m_outline->hide();
        if (!guard)
            return;
        if (m_applying) {
            m_view->markDirty();
            return;
        }
        applyFrame(true);
    }

    QRect rowRect(qsizetype row) const
    {
        return rowRectAt(row, progress());
    }

    // Keep protected base access in the friend member context for older compilers.
    int rowSpacingOffset(qsizetype row) const
    {
        return rowRect(row).y() - m_view->VirtualItemView::geometryForViewRow(row).y();
    }

    struct PlaceholderBoundary {
        QRect geometry;
        bool trailingEdge = true;
    };

    // A subtree tail can share its flattened boundary with an ancestor's next
    // sibling. Indent the outline by the actual drop parent, not the source node.
    QRect placeholderOutlineRect() const
    {
        if (!isActive() || !m_outlineAmbiguous || m_targetRow < 0 || !m_source.isValid())
            return {};
        const qsizetype destination = m_preview.destination();
        const qsizetype count = m_view->viewItemCount();
        if (destination <= 0 || destination > count)
            return {};
        const int depth = m_targetParent == m_view->rootIndex()
            ? 0 : m_view->itemDepth(m_targetParent) + 1;
        // Reparenting at the source's leading boundary leaves the original
        // source slot open. Anchor to the first row after the hidden subtree.
        const qsizetype boundary = destination == m_preview.source()
            ? m_preview.sourceEnd() : destination;
        QRect gap = placeholderGapBefore(boundary, progress());
        const int left = int(qMin<qint64>(m_view->viewport()->width(),
            qint64(depth) * m_view->indentation()));
        gap.setLeft(left);
        return gap;
    }

    // Bound the actual openings between moving neighbours, including the
    // shrinking source gap and gaps left during a quick change of destination.
    QVector<PlaceholderBoundary> placeholderBoundaries(int lineWidth) const
    {
        QVector<PlaceholderBoundary> boundaries;
        if (!isActive() || !m_source.isValid())
            return boundaries;
        const qsizetype count = m_view->viewItemCount();
        if (count <= 0)
            return boundaries;
        QVector<qsizetype> rows{m_preview.sourceEnd(), m_preview.destination(), count};
        for (const auto &item : m_view->materializedItems())
            rows.append(m_view->viewItemForIndex(item.index));
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        const qreal current = progress();
        const auto endpoints = m_view->horizontalGridLineRange(m_view->rowGridLineInset(m_source));
        const int left = endpoints.first;
        const int right = endpoints.second;
        const int width = right - left;
        for (qsizetype row : rows) {
            if (row < 0 || row > count || m_preview.containsSource(row))
                continue;
            const qsizetype previous = m_preview.containsSource(row - 1)
                ? m_preview.source() - 1 : row - 1;
            const QRect gap = placeholderGapBefore(row, current);
            if (gap.isEmpty())
                continue;
            const int targetDepth = m_targetParent == m_view->rootIndex()
                ? 0 : m_view->itemDepth(m_targetParent) + 1;
            if (!m_view->rowGridLineDepthFilter().isEmpty()
                && !m_view->rowGridLineDepthFilter().contains(targetDepth))
                continue;
            // Reuse a preceding trailing edge; at the start of the tree there
            // is no preceding row, so anchor the border to the leading edge.
            boundaries.append({QRect(left, previous >= 0 ? gap.top() - lineWidth : gap.top(),
                                     width, lineWidth), previous >= 0});
            boundaries.append({QRect(left, gap.bottom() + 1 - lineWidth, width, lineWidth), true});
        }
        return boundaries;
    }

    // Inverse queries use final positions, which are ordered even when newly
    // materialized rows have no interpolation sample. Existing samples are added below.
    QRect rowRectAt(qsizetype row, qreal position) const
    {
        QRect rect = m_view->VirtualItemView::geometryForViewRow(row);
        if (isActive()) {
            if (m_preview.containsSource(row))
                rect.moveTop(-rect.height() - m_view->m_rowLayout->spacingAfter(row) - 1);
            else {
                const qreal y = qreal(rect.y()) + m_preview.offset(row, position);
                rect.moveTop(qRound(qBound<qreal>(std::numeric_limits<int>::min() / 2,
                    y, std::numeric_limits<int>::max() / 2)));
            }
        }
        return rect;
    }

    // Inverse queries over the two ordered blocks avoid walking a large subtree.
    // Include displaced rows and their spacing hosts, even outside natural overscan.
    void augmentRanges(QVector<VisibleRange> &ranges) const
    {
        if (!isActive())
            return;
        detail::PerformanceScope performance(m_view, "layout.tree-drag-ranges");
        const int bottom = m_view->viewport()->height();
        const auto append = [&](qsizetype first, qsizetype end) {
            qsizetype low = first, high = end;
            while (low < high) {
                const qsizetype mid = low + (high - low) / 2;
                if (rowRectAt(mid, 1.0).bottom() < 0)
                    low = mid + 1;
                else
                    high = mid;
            }
            const qsizetype begin = qMax(first, low - 1 - m_view->overscanBefore());
            high = end;
            while (low < high) {
                const qsizetype mid = low + (high - low) / 2;
                if (rowRectAt(mid, 1.0).top() < bottom)
                    low = mid + 1;
                else
                    high = mid;
            }
            const qsizetype last = qMin(end - 1, low + m_view->overscanAfter());
            if (begin <= last)
                ranges.append({begin, last});
        };
        append(0, m_preview.source());
        append(m_preview.sourceEnd(), m_view->viewItemCount());
        const qreal current = progress();
        for (qsizetype row : m_sampledRows) {
            if (row < 0 || row >= m_view->viewItemCount() || m_preview.containsSource(row))
                continue;
            const QRect rect = rowRectAt(row, current);
            if (rect.top() < bottom
                && rect.bottom() + m_view->m_rowLayout->spacingAfter(row) >= 0)
                ranges.append({row, row});
        }
        std::sort(ranges.begin(), ranges.end(), [](const VisibleRange &a, const VisibleRange &b) {
            return a.first < b.first;
        });
        QVector<VisibleRange> merged;
        for (const VisibleRange &range : ranges) {
            if (!range.isValid())
                continue;
            if (!merged.isEmpty() && range.first <= merged.last().last + 1)
                merged.last().last = qMax(merged.last().last, range.last);
            else
                merged.append(range);
        }
        ranges.swap(merged);
    }

    void applyFrame(bool refreshRanges = false)
    {
        detail::PerformanceScope performance(m_view, "layout.drag-animation");
        if (m_applying)
            return;
        const QPointer<VirtualTreeView> self(m_view);
        const QPointer<QAbstractItemModel> model(m_view->model());
        const quint64 serial = m_view->modelChangeSerial();
        const quint64 revision = m_revision;
        const QPersistentModelIndex root(m_view->rootIndex());
        const auto current = [=]() {
            return self && model && self->model() == model.data()
                && self->modelChangeSerial() == serial && self->rootIndex() == root
                && m_revision == revision;
        };
        if (!current())
            return;
        m_applying = true;
        const auto cleanup = qScopeGuard([self, this]() {
            if (self)
                m_applying = false;
        });
        const auto items = m_view->materializedItems();
        QVector<QPointer<QWidget>> widgets;
        for (const auto &item : items)
            widgets.append(item.widget);
        for (qsizetype i = 0; i < items.size(); ++i) {
            if (!current())
                return;
            const auto &item = items.at(i);
            const auto widget = widgets.at(i);
            if (!widget || !item.index.isValid()
                || m_view->widgetForIndex(item.index) != widget.data())
                continue;
            const qsizetype row = m_view->viewItemForIndex(item.index);
            if (row < 0)
                continue;
            QWidget *parent = widget->parentWidget();
            const int parentY = parent
                ? m_view->viewport()->mapFromGlobal(parent->mapToGlobal(QPoint())).y() : 0;
            detail::performanceCall(m_view, "items.widget.move", [&] {
                widget->move(widget->x(), rowRect(row).y() - parentY);
            });
        }
        if (!current())
            return;
        m_view->updateRowSpacingVisualGeometry([this](qsizetype row) {
            return rowSpacingOffset(row);
        });
        if (!current())
            return;
        m_view->syncRowGridLines();
        if (!current())
            return;
        syncPlaceholderOutline();
        if (!current())
            return;
        m_view->viewport()->update();
        if (refreshRanges)
            m_view->markDirty();
    }

    bool update(const VirtualItemView::DropTarget &target, const QPoint &position,
                Qt::DropAction action, const QList<QPersistentModelIndex> &sources)
    {
        detail::PerformanceScope performance(m_view, "drag.tree-target");
        const auto indicator = [this]() { clear(); return false; };
        if (mode != VirtualTreeView::DragFeedbackMode::RowMoveAnimation
            || action != Qt::MoveAction || sources.isEmpty() || !m_view->model()
            || !target.isValid() || target.ontoItem || target.cellDrop || target.column >= 0
            || !m_view->viewport()->rect().contains(position)
            || m_view->m_rowLayout->hasTransientGeometry())
            return indicator();
        const QPersistentModelIndex source(QModelIndex(sources.first()).siblingAtColumn(0));
        for (const auto &index : sources) {
            if (!index.isValid() || index.model() != m_view->model()
                || QModelIndex(index).siblingAtColumn(0) != source)
                return indicator();
        }
        const qsizetype first = m_view->viewItemForIndex(source);
        if (first < 0 || target.row < 0 || target.row > m_view->model()->rowCount(target.parent))
            return indicator();
        // Native single-node drags collapse their source before capture. If
        // business code expands it again, retain the indicator fallback.
        const qsizetype end = first + 1;
        if (m_view->visibleSubtreeEnd(source) != end)
            return indicator();
        qsizetype destination = -1;
        if (target.row < m_view->model()->rowCount(target.parent))
            destination = m_view->viewItemForIndex(m_view->model()->index(target.row, 0, target.parent));
        else if (target.parent == m_view->rootIndex())
            destination = m_view->viewItemCount();
        else if (m_view->viewItemForIndex(target.parent) >= 0 && m_view->isExpanded(target.parent))
            destination = m_view->visibleSubtreeEnd(target.parent);
        if (destination < 0 || (destination > first && destination < end))
            return indicator();
        // Probe only other hierarchy levels that end at the same visible boundary.
        // Use the actual native MIME and model policy, never flags alone. Do this
        // on target updates rather than invoking business code on animation frames.
        const QPointer<VirtualTreeView> guard(m_view);
        const QPointer<QAbstractItemModel> model(m_view->model());
        const quint64 serial = m_view->modelChangeSerial();
        const quint64 revision = m_revision;
        const QPersistentModelIndex root(m_view->rootIndex());
        const auto current = [=]() {
            return guard && model && guard->model() == model.data()
                && guard->modelChangeSerial() == serial && guard->rootIndex() == root
                && m_revision == revision && source.isValid();
        };
        bool ambiguous = false;
        QModelIndex node = destination > 0 ? m_view->viewIndex(destination - 1) : QModelIndex();
        while (node.isValid() && node != root && m_view->visibleSubtreeEnd(node) == destination) {
            VirtualItemView::DropTarget alternative;
            alternative.parent = node.parent();
            alternative.row = node.row() + 1;
            if (alternative.parent != target.parent || alternative.row != target.row) {
                // The source's original slot still identifies another depth,
                // although an actual move there is rejected as a no-op. Query
                // model permission without that view-level no-op suppression.
                const bool originalSlot = alternative.parent == source.parent()
                    && ((destination == first && alternative.row == source.row())
                        || (destination == end && alternative.row == source.row() + 1));
                const bool accepted = m_view->canDropAtCurrentDragTarget(alternative, originalSlot);
                if (!current())
                    return true;
                if (accepted) {
                    ambiguous = true;
                    break;
                }
            }
            node = node.parent();
        }
        // Equal flattened destinations can still mean different target levels.
        // Refresh the outline immediately without restarting the displacement tween.
        if (m_targetParent != target.parent || m_targetRow != target.row
            || m_outlineAmbiguous != ambiguous) {
            m_targetParent = target.parent;
            m_targetRow = target.row;
            m_outlineAmbiguous = ambiguous;
            ++m_revision;
        }
        const qreal extent = qreal(m_view->m_rowLayout->offsetOf(end)
            - m_view->m_rowLayout->offsetOf(first));
        if (isActive() && m_source == source && m_preview.sourceEnd() == end
            && m_preview.destination() == destination && m_preview.extent() == extent) {
            applyFrame();
            return true;
        }
        QVector<qsizetype> rows;
        for (const auto &item : m_view->materializedItems())
            rows.append(m_view->viewItemForIndex(item.index));
        const qreal previous = progress();
        m_animation->stop();
        m_preview.retarget(first, end, destination, extent, rows, previous);
        m_sampledRows = rows;
        ++m_revision;
        m_source = source;
        m_clock.invalidate();
        const QPointer<VirtualTreeView> self(m_view);
        if (duration > 0) {
            m_animation->setDuration(duration);
            m_animation->setEasingCurve(easing);
            m_clock.start();
            m_animation->start();
            if (!self)
                return true;
        }
        applyFrame(true);
        return true;
    }

private:
    // Natural row spacing is not part of the temporary opening.
    QRect placeholderGapBefore(qsizetype row, qreal position) const
    {
        const qsizetype count = m_view->viewItemCount();
        if (row < 0 || row > count || count <= 0 || m_preview.containsSource(row))
            return {};
        const qsizetype previous = m_preview.containsSource(row - 1)
            ? m_preview.source() - 1 : row - 1;
        const int top = previous >= 0
            ? rowRectAt(previous, position).bottom() + 1 + m_view->m_rowLayout->spacingAfter(previous)
            : m_view->VirtualItemView::geometryForViewRow(0).top();
        const int bottom = row < count ? rowRectAt(row, position).top()
            : m_view->VirtualItemView::geometryForViewRow(count - 1).bottom() + 1;
        return QRect(0, top, m_view->viewport()->width(), qMax(0, bottom - top));
    }

    void syncPlaceholderOutline()
    {
        const QRect rect = placeholderOutlineRect();
        if (rect.isEmpty() || !rect.intersects(m_view->viewport()->rect())
            || m_view->dropIndicatorWidth() <= 0) {
            if (m_outline)
                m_outline->hide();
            return;
        }
        if (!m_outline)
            m_outline = new TreeDragPlaceholderOutline(m_view);
        const QPointer<VirtualTreeView> guard(m_view);
        const QColor color = m_view->dropIndicatorColor().isValid()
            ? m_view->dropIndicatorColor() : m_view->palette().color(QPalette::Highlight);
        m_outline->setFrame(rect, color, m_view->dropIndicatorWidth());
        m_outline->setGeometry(m_view->viewport()->geometry());
        if (!guard || !m_outline)
            return;
        m_outline->show();
        if (guard && m_outline)
            m_outline->raise();
    }

    qreal progress() const
    {
        if (!m_clock.isValid() || m_animation->state() != QAbstractAnimation::Running || duration <= 0)
            return 1.0;
        const qint64 elapsed = qMax<qint64>(m_clock.elapsed(), m_animation->currentTime());
        return easing.valueForProgress(qBound<qreal>(0, qreal(elapsed) / duration, 1));
    }

    VirtualTreeView *m_view;
    QVariantAnimation *m_animation = nullptr;
    RowDragPreview m_preview;
    QVector<qsizetype> m_sampledRows;
    QPersistentModelIndex m_source;
    QPersistentModelIndex m_targetParent;
    int m_targetRow = -1;
    bool m_outlineAmbiguous = false;
    QPointer<TreeDragPlaceholderOutline> m_outline;
    QElapsedTimer m_clock;
    quint64 m_revision = 0;
    bool m_applying = false;
};

} // namespace viv
