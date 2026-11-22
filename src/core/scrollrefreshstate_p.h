#pragma once

#include <virtualitemviews/virtualitemview.h>

namespace viv {
namespace detail {

struct ScrollRefreshState
{
    using State = VirtualItemView::RefreshState;
    State state = State::Idle;
    quint64 sequence = 0;
    quint64 active = 0;
    qsizetype initialCount = 0;
    qint64 initialExtent = 0;
    int threshold = 64;
    int distance = 0;
    bool hasMore = true;
    bool paused = false;
    /// 外部首屏或手动刷新进行中，暂停自动分页但不影响失败暂停
    bool suspended = false;
    bool terminalFooter = false;
    bool bottomConsumed = false;
    bool pageAddedItems = false;
    Qt::Edges publishedEdges;

    static Qt::Edges edges(qint64 offset, qint64 maximum, Qt::Edge first, Qt::Edge last)
    {
        Qt::Edges result;
        if (offset <= 0) result |= first;
        if (offset >= maximum) result |= last;
        return result;
    }

    qreal progress() const { return qreal(qMin(distance, threshold)) / threshold; }

    bool edgeChanged(Qt::Edge edge, bool reached)
    {
        if (publishedEdges.testFlag(edge) == reached) return false;
        publishedEdges.setFlag(edge, reached);
        if (!reached && edge == Qt::BottomEdge) bottomConsumed = false;
        return true;
    }

    void pull(int pixels)
    {
        distance = qMax(0, pixels);
        state = distance >= threshold ? State::Armed : State::Pulling;
    }

    void cancelPull()
    {
        distance = 0;
        if (!active) state = State::Idle;
    }

    quint64 begin(State request, qsizetype count, qint64 extent = 0)
    {
        if (active || (request == State::Refreshing && state != State::Armed)) return 0;
        distance = 0;
        state = request;
        initialCount = count;
        initialExtent = extent;
        pageAddedItems = false;
        active = ++sequence;
        if (request == State::LoadingMore) bottomConsumed = true;
        return active;
    }

    bool canLoad(bool atBottom, bool continueAtBottom) const
    {
        return state == State::Idle && atBottom && hasMore && !paused && !suspended
            && (!bottomConsumed || continueAtBottom);
    }

    bool canContinueAtBottom(bool bounded, qint64 maximumOffset, qint64 extent) const
    {
        return bounded && (maximumOffset == 0 || (pageAddedItems && extent == initialExtent));
    }

    bool canLoadAtOffset(qint64 offset, qint64 maximumOffset, qint64 extent, bool bounded) const
    {
        return canLoad(offset >= maximumOffset, canContinueAtBottom(bounded, maximumOffset, extent));
    }

    bool finish(quint64 id, State expected, bool more, bool success, qsizetype count, bool inferEnd = false)
    {
        if (!id || id != active || state != expected) return false;
        if (success) {
            terminalFooter = inferEnd && expected == State::LoadingMore && count <= initialCount;
            hasMore = more && !terminalFooter;
        }
        paused = !success || (expected == State::LoadingMore && count <= initialCount);
        pageAddedItems = success && expected == State::LoadingMore && count > initialCount;
        if (success && expected == State::Refreshing) bottomConsumed = false;
        active = 0;
        state = State::Idle;
        return true;
    }

    void reset()
    {
        active = 0;
        cancelPull();
        hasMore = true;
        terminalFooter = false;
        paused = bottomConsumed = suspended = false;
        pageAddedItems = false;
    }
};

} // namespace detail
} // namespace viv
