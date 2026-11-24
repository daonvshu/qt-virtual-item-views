#pragma once

#include <virtualitemviews/virtualitemview.h>

namespace viv {
namespace detail {

struct ScrollRefreshState
{
    using State = VirtualItemView::RefreshState;
    using PauseReason = VirtualItemView::LoadMorePauseReason;
    State state = State::Idle;
    quint64 sequence = 0;
    quint64 active = 0;
    qsizetype initialCount = 0;
    qint64 initialExtent = 0;
    int threshold = 64;
    int distance = 0;
    bool hasMore = true;
    PauseReason stopReason = PauseReason::None;
    bool externalPage = false;
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

    quint64 begin(State request, qsizetype count, qint64 extent = 0, bool explicitRefresh = false)
    {
        if (request == State::Refreshing && explicitRefresh) {
            externalPage = false;
            stopReason = PauseReason::None;
        } else if (active || (request == State::Refreshing && state != State::Armed)) {
            return 0;
        }
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
        return !active && state == State::Idle && atBottom && hasMore && pauseReason() == PauseReason::None
            && (!bottomConsumed || continueAtBottom);
    }

    PauseReason pauseReason() const
    {
        if (suspended) return PauseReason::Suspended;
        if (externalPage) return PauseReason::ExternalPage;
        return stopReason;
    }

    bool isCurrent(quint64 id) const { return id && active == id; }

    bool cancelRequest(quint64 id)
    {
        if (!isCurrent(id)) return false;
        active = 0;
        externalPage = false;
        cancelPull();
        stopReason = PauseReason::RequestCancelled;
        pageAddedItems = false;
        return true;
    }

    quint64 beginExternalPage()
    {
        reset();
        externalPage = true;
        hasMore = false;
        active = ++sequence;
        return active;
    }

    bool finishExternalPage(quint64 id, bool more, bool success)
    {
        if (!externalPage || !isCurrent(id)) return false;
        externalPage = false;
        active = 0;
        hasMore = success && more;
        stopReason = success ? PauseReason::None : PauseReason::ExternalPageFailed;
        return true;
    }

    bool setHasMore(bool more)
    {
        if (hasMore == more) return false;
        hasMore = more;
        if (more) {
            bottomConsumed = false;
            terminalFooter = false;
        }
        return true;
    }

    bool retryLoadMore()
    {
        if (active || (stopReason != PauseReason::RequestFailed && stopReason != PauseReason::EmptyPage
            && stopReason != PauseReason::RequestCancelled)) return false;
        stopReason = PauseReason::None;
        bottomConsumed = false;
        return true;
    }

    bool canContinueAtBottom(bool bounded, qint64 maximumOffset, qint64 extent) const
    {
        return bounded && (maximumOffset == 0 || (pageAddedItems && extent == initialExtent));
    }

    int inlineFooterShift(int height, qint64 viewportExtent, qint64 contentExtent) const
    {
        // Appended rows must not push older rows up while the footer is still visible.
        if (state == State::LoadingMore || pageAddedItems)
            contentExtent = qMin(contentExtent, initialExtent);
        const qint64 freeSpace = qMax(qint64(0), viewportExtent - contentExtent);
        return height - int(qMin(qint64(height), freeSpace));
    }

    bool canLoadAtOffset(qint64 offset, qint64 maximumOffset, qint64 extent, bool bounded) const
    {
        return canLoad(offset >= maximumOffset, canContinueAtBottom(bounded, maximumOffset, extent));
    }

    bool finish(quint64 id, State expected, bool more, bool success, qsizetype count, bool inferEnd = false)
    {
        if (externalPage || !isCurrent(id) || state != expected) return false;
        if (success) {
            terminalFooter = inferEnd && expected == State::LoadingMore && count <= initialCount;
            hasMore = more && !terminalFooter;
        }
        stopReason = PauseReason::None;
        if (!success) stopReason = expected == State::Refreshing ? PauseReason::RefreshFailed : PauseReason::RequestFailed;
        else if (hasMore && expected == State::LoadingMore && count <= initialCount) stopReason = PauseReason::EmptyPage;
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
        externalPage = bottomConsumed = false;
        stopReason = PauseReason::None;
        pageAddedItems = false;
    }
};

} // namespace detail
} // namespace viv
