#include "scrollrefreshstate_p.h"

#include <QDebug>
#include <QtGlobal>

#include <virtualitemviews/virtualtableview.h>
#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTouchEvent>
#include <QTextDocument>
#include <QVariantAnimation>
#include <QWheelEvent>

namespace viv {

using RefreshState = VirtualItemView::RefreshState;
using IndicatorMode = VirtualItemView::IndicatorMode;
using PauseReason = VirtualItemView::LoadMorePauseReason;

// Touch is translated into viewport mouse input. Only bypass passive labels and
// layout hosts; a label inside a button must still leave the gesture to the button.
static bool passiveTouchTarget(QWidget *viewport, const QPoint &point)
{
    for (QWidget *w = viewport->childAt(point); w && w != viewport; w = w->parentWidget()) {
        if (w->focusPolicy() != Qt::NoFocus || w->testAttribute(Qt::WA_AcceptTouchEvents))
            return false;
        if (w->property("_vivTransparentHost").toBool())
            continue;
        const auto *label = qobject_cast<QLabel *>(w);
        if (!label || label->metaObject() != &QLabel::staticMetaObject)
            return false;
        const auto flags = label->textInteractionFlags();
        if ((flags & (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard))
            || (flags != Qt::NoTextInteraction && label->textFormat() != Qt::PlainText
                && (label->textFormat() != Qt::AutoText || Qt::mightBeRichText(label->text()))))
            return false;
    }
    return true;
}

class ViewRefreshController : public QObject
{
public:
    explicit ViewRefreshController(VirtualItemView *owner) : QObject(owner), view(owner)
    {
        for (Indicator *slot : {&top, &bottom}) {
            slot->animation.setDuration(200);
            slot->animation.setEasingCurve(QEasingCurve::OutCubic);
            connect(&slot->animation, &QVariantAnimation::valueChanged, this, [this, slot](const QVariant &value) {
                slot->height = value.toInt();
                applyGeometry();
            });
        }
    }

    VirtualItemView *view;
    detail::ScrollRefreshState machine;
    RefreshState publishedState = RefreshState::Idle;
    PauseReason publishedPause = PauseReason::None;
    qreal publishedProgress = 0;
    bool pullEnabled = false;
    bool loadEnabled = false;
    bool scheduled = false;
    bool tracking = false;
    bool captured = false;
    bool previousTouchAttribute = false;
    QPoint origin;
    bool viewportTouch = false;
    int restingHeight = 36;
    int maximumHeight = 112;
    bool fillingGap = false;
    qint64 loadOffset = 0;
    qint64 fillOffset = 0;
    qint64 lastFillOffset = 0;

    struct Indicator {
        QPointer<QWidget> widget;
        bool builtIn = false;
        IndicatorMode mode = IndicatorMode::Overlay;
        QVariantAnimation animation;
        int height = 0;
        int target = 0;
        QPointer<QWidget> clip;
    } top, bottom;

    void reveal(Indicator &slot, int target, bool followPointer = false)
    {
        target = qBound(0, target, maximumHeight);
        if (!followPointer && slot.target == target) return;
        slot.animation.stop();
        slot.target = target;
        if (followPointer) {
            slot.height = target;
        } else if (slot.height != target) {
            {
                const QSignalBlocker guard(&slot.animation);
                slot.animation.setStartValue(slot.height);
                slot.animation.setEndValue(target);
            }
            slot.animation.start();
        }
    }

    void syncPresentation()
    {
        const bool pulling = captured && (machine.state == RefreshState::Armed || machine.state == RefreshState::Pulling);
        const bool loading = loadEnabled && machine.state == RefreshState::LoadingMore
            && (bottom.mode == IndicatorMode::Overlay || view->verticalOffset() >= loadOffset);
        const bool terminal = loadEnabled && machine.terminalFooter && bottom.mode == IndicatorMode::Inline
            && machine.state == RefreshState::Idle && view->verticalOffset() >= view->maximumVerticalOffset();
        reveal(top, pulling ? machine.distance : machine.state == RefreshState::Refreshing ? restingHeight : 0, pulling);
        reveal(bottom, loading || terminal ? restingHeight : 0);
        applyGeometry();
    }

    void applyGeometry()
    {
        if (view->m_destroying) return;
        if (!top.height && !bottom.height && !view->m_refreshVisualOffset && !fillingGap) {
            layoutIndicators();
            return;
        }
        const int paneHeight = qMax(0, scrollingPane().height());
        const int leading = top.mode == IndicatorMode::Inline ? qMin(top.height, paneHeight) : 0;
        const int trailing = bottom.mode == IndicatorMode::Inline ? qMin(bottom.height, paneHeight) : 0;
        const int footerShift = machine.inlineFooterShift(trailing, view->viewportMainExtent(),
                                                         view->contentExtent());
        const int shift = footerShift - leading;
        bool changed = view->m_refreshVisualOffset != shift;
        view->m_refreshVisualOffset = shift;
        if (fillingGap) {
            if (view->verticalOffset() == lastFillOffset) {
                lastFillOffset = qBound(qint64(0), fillOffset - footerShift, view->maximumVerticalOffset());
                if (lastFillOffset != view->verticalOffset()) {
                    view->m_scrollOffset = lastFillOffset;
                    changed = true;
                }
            } else fillingGap = false;
            if (!trailing) fillingGap = false;
        }
        if (changed) view->markDirty();
        layoutIndicators();
    }

    QRect scrollingPane() const
    {
        return view->frozenRows() || view->frozenBottomRows()
            ? view->itemPaneRect(ItemPane::Type::Scrollable) : view->viewport()->rect();
    }

    void layoutIndicator(Indicator &indicator, bool atTop, const QRect &pane)
    {
        const auto state = machine.state;
        if (!indicator.height || pane.isEmpty()) {
            if (indicator.clip) indicator.clip->hide();
            return;
        }
        if (!indicator.clip) {
            indicator.clip = new QWidget(view);
            indicator.clip->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        if (!indicator.widget) {
            auto *label = new QLabel(indicator.clip);
            label->setObjectName(atTop ? QStringLiteral("vivRefreshIndicator") : QStringLiteral("vivLoadMoreIndicator"));
            label->setAlignment(Qt::AlignCenter);
            label->setMargin(5);
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
            indicator.widget = label;
            indicator.builtIn = true;
        }
        if (indicator.widget->parentWidget() != indicator.clip) indicator.widget->setParent(indicator.clip);
        const bool terminal = !atTop && machine.terminalFooter && indicator.mode == IndicatorMode::Inline;
        if (indicator.widget->property("vivNoMore").toBool() != terminal)
            indicator.widget->setProperty("vivNoMore", terminal);
        if (indicator.builtIn) {
            auto *label = static_cast<QLabel *>(indicator.widget.data());
            label->setAutoFillBackground(indicator.mode == IndicatorMode::Inline);
            QString text;
            if (!atTop) text = terminal ? view->tr("No more items") : view->tr("Loading more…");
            else switch (state) {
            case RefreshState::Pulling: text = view->tr("Pull to refresh (%1%)").arg(qRound(machine.progress() * 100)); break;
            case RefreshState::Armed: text = view->tr("Release to refresh"); break;
            case RefreshState::Refreshing: text = view->tr("Refreshing…"); break;
            case RefreshState::LoadingMore:
            case RefreshState::Idle: break;
            }
            if (!text.isEmpty()) label->setText(text);
        }
        const int height = qMin(indicator.height, pane.height());
        indicator.clip->setGeometry(pane.left(), atTop ? pane.top() : pane.bottom() - height + 1, pane.width(), height);
        const int body = atTop && captured ? height : qMax(restingHeight, height);
        indicator.widget->setGeometry(0, atTop ? height - body : 0, pane.width(), body);
        indicator.widget->show();
        indicator.clip->show();
    }

    void layoutIndicators()
    {
        if (!top.height && !bottom.height && !top.clip && !bottom.clip) return;
        const QRect pane = scrollingPane().translated(view->viewport()->pos());
        layoutIndicator(top, true, pane);
        layoutIndicator(bottom, false, pane);
        view->raiseRefreshIndicators();
    }

    void replace(Indicator &slot, QWidget *widget)
    {
        if (widget && (widget == view->viewport() || widget->isAncestorOf(view->viewport())
                       || widget == top.widget || widget == bottom.widget)) return;
        if (slot.widget) {
            slot.widget->hide();
            slot.widget->deleteLater();
        }
        slot.widget = widget;
        slot.builtIn = false;
        if (widget) {
            widget->hide();
            widget->setParent(view->viewport());
            widget->setAttribute(Qt::WA_TransparentForMouseEvents);
            widget->setFocusPolicy(Qt::NoFocus);
        }
        layoutIndicators();
    }

    void notify()
    {
        QPointer<ViewRefreshController> self(this);
        syncPresentation();
        if (self && publishedState != machine.state) {
            publishedState = machine.state;
            emit view->refreshStateChanged(publishedState);
        }
        if (self && publishedProgress != machine.progress()) {
            publishedProgress = machine.progress();
            emit view->pullProgressChanged(publishedProgress);
        }
        if (self && publishedPause != machine.pauseReason()) {
            publishedPause = machine.pauseReason();
            emit view->loadMorePauseReasonChanged(publishedPause);
        }
    }

    quint64 request(RefreshState kind, bool explicitRefresh = false)
    {
        const quint64 id = machine.begin(kind, view->viewItemCount(), view->contentExtent(), explicitRefresh);
        if (!id) return 0;
        if (kind == RefreshState::LoadingMore) loadOffset = view->verticalOffset();
        QPointer<ViewRefreshController> self(this);
        notify();
        if (!self || machine.active != id) return 0;
        if (kind == RefreshState::Refreshing) emit view->refreshRequested(id);
        else emit view->loadMoreRequested(id);
        return id;
    }

    void complete(quint64 id, RefreshState kind, bool more, bool success, bool inferEnd = false)
    {
        if (!id || machine.active != id || !view->model()) return;
        const auto count = view->viewItemCount();
        if (!machine.finish(id, kind, more, success, count, inferEnd)) return;
        if (success && kind == RefreshState::LoadingMore && count > machine.initialCount
            && bottom.mode == IndicatorMode::Inline && view->m_refreshVisualOffset > 0) {
            lastFillOffset = view->verticalOffset();
            fillOffset = view->visualVerticalOffset();
            fillingGap = true;
        }
        view->scheduleScrollUpdate();
        notify();
    }

    void clearGesture()
    {
        const bool wasTracking = tracking;
        if (wasTracking || viewportTouch || view->m_deferPressSelection)
            view->m_pressedIndex = QPersistentModelIndex();
        view->m_deferPressSelection = false;
        viewportTouch = false;
        tracking = captured = false;
        machine.cancelPull();
        if (wasTracking)
            view->scheduleScrollUpdate();
    }

    void cancel()
    {
        clearGesture();
        notify();
    }

    bool eligible(const QPoint &point) const
    {
        return pullEnabled && view->model() && !machine.active && view->verticalOffset() == 0
            && scrollingPane().contains(point)
            && !view->canStartDrag(view->indexAt(point));
    }

    void evaluate()
    {
        scheduled = false;
        if (view->m_destroying || view->m_inRelayout)
            return;
        if (view->m_relayoutScheduled || view->m_autoHeightScrollBarSyncPending) {
            view->scheduleScrollUpdate();
            return;
        }
        QPointer<ViewRefreshController> self(this);
        view->publishScrollPosition();
        if (!self) return;
        syncPresentation();
        if (!self || !loadEnabled || tracking || !view->model() || !view->isVisible()
            || !view->viewport()->isVisible() || view->viewport()->size().isEmpty())
            return;
        if (view->m_relayoutScheduled || view->m_autoHeightScrollBarSyncPending) {
            view->scheduleScrollUpdate();
            return;
        }
        if (qEnvironmentVariableIsSet("VIV_REFRESH_DEBUG")) {
            qDebug().nospace() << "[viv-refresh] evaluate offset=" << view->verticalOffset()
                               << " max=" << view->maximumVerticalOffset()
                               << " hasMore=" << machine.hasMore
                               << " pauseReason=" << machine.pauseReason()
                               << " suspended=" << machine.suspended
                               << " consumed=" << machine.bottomConsumed
                               << " state=" << int(machine.state);
        }
        const bool bounded = !view->autoHeightEnabled() || view->autoHeightMaximumHeight() > 0;
        if (machine.canLoadAtOffset(view->verticalOffset(), view->maximumVerticalOffset(),
                                    view->contentExtent(), bounded))
            request(RefreshState::LoadingMore);
    }
};

void VirtualItemView::initScrollRefresh()
{
    m_refreshController = new ViewRefreshController(this);
}

void VirtualItemView::raiseRefreshIndicators() const
{
    const auto *controller = m_refreshController;
    if (!controller || m_destroying)
        return;
    for (const auto *indicator : {&controller->top, &controller->bottom}) {
        if (indicator->clip && indicator->clip->isVisible())
            indicator->clip->raise();
    }
}

Qt::Edges VirtualItemView::scrollEdges() const
{
    using detail::ScrollRefreshState;
    auto edges = ScrollRefreshState::edges(verticalOffset(), maximumVerticalOffset(), Qt::TopEdge, Qt::BottomEdge);
    if (const auto *table = qobject_cast<const VirtualTableView *>(this))
        edges |= ScrollRefreshState::edges(table->horizontalOffset(), table->maximumHorizontalOffset(), Qt::LeftEdge, Qt::RightEdge);
    return edges;
}

void VirtualItemView::publishScrollPosition()
{
    auto *controller = m_refreshController;
    if (!controller || m_destroying || m_inRelayout || m_autoHeightScrollBarSyncPending) return;
    const auto edges = scrollEdges();
    QPointer<VirtualItemView> self(this);
    for (const auto edge : {Qt::TopEdge, Qt::BottomEdge, Qt::LeftEdge, Qt::RightEdge}) {
        const bool reached = edges.testFlag(edge);
        if (!controller->machine.edgeChanged(edge, reached)) continue;
        if (reached) emit scrollEdgeReached(edge);
        else emit scrollEdgeLeft(edge);
        if (!self || edges != scrollEdges()) return;
    }
    if (controller->captured && !edges.testFlag(Qt::TopEdge)) controller->cancel();
}

void VirtualItemView::scheduleScrollUpdate()
{
    auto *controller = m_refreshController;
    if (m_destroying || !controller || controller->scheduled) return;
    controller->scheduled = true;
    QMetaObject::invokeMethod(controller, [controller] { controller->evaluate(); }, Qt::QueuedConnection);
}

void VirtualItemView::resetRefreshModel()
{
    if (m_destroying) return;
    m_refreshController->fillingGap = false;
    m_refreshController->machine.reset();
    m_refreshController->cancel();
}

void VirtualItemView::setPullToRefreshEnabled(bool enabled)
{
    auto *controller = m_refreshController;
    if (controller->pullEnabled == enabled) return;
    controller->pullEnabled = enabled;
    if (enabled) controller->previousTouchAttribute = viewport()->testAttribute(Qt::WA_AcceptTouchEvents);
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents, enabled || controller->previousTouchAttribute);
    if (!enabled) controller->cancel();
}

bool VirtualItemView::pullToRefreshEnabled() const { return m_refreshController->pullEnabled; }
int VirtualItemView::pullToRefreshThreshold() const { return m_refreshController->machine.threshold; }
VirtualItemView::RefreshState VirtualItemView::refreshState() const { return m_refreshController->machine.state; }
qreal VirtualItemView::pullProgress() const { return m_refreshController->machine.progress(); }
bool VirtualItemView::loadMoreEnabled() const { return m_refreshController->loadEnabled; }

void VirtualItemView::setIndicatorModes(IndicatorMode refresh, IndicatorMode loadMore)
{
    m_refreshController->top.mode = refresh;
    m_refreshController->bottom.mode = loadMore;
    m_refreshController->syncPresentation();
}

void VirtualItemView::setIndicatorHeights(int restingHeight, int maximumHeight)
{
    auto *controller = m_refreshController;
    controller->maximumHeight = qBound(1, maximumHeight, QWIDGETSIZE_MAX);
    controller->restingHeight = qBound(1, restingHeight, controller->maximumHeight);
    controller->syncPresentation();
}

void VirtualItemView::setPullToRefreshThreshold(int pixels)
{
    auto *controller = m_refreshController;
    controller->machine.threshold = qMax(1, pixels);
    if (controller->captured) controller->machine.pull(controller->machine.distance);
    controller->notify();
}

void VirtualItemView::setLoadMoreEnabled(bool enabled)
{
    if (m_refreshController->loadEnabled == enabled) return;
    m_refreshController->loadEnabled = enabled;
    m_refreshController->syncPresentation();
    if (enabled) scheduleScrollUpdate();
}

void VirtualItemView::setRefreshIndicator(QWidget *indicator) { m_refreshController->replace(m_refreshController->top, indicator); }
void VirtualItemView::setLoadMoreIndicator(QWidget *indicator) { m_refreshController->replace(m_refreshController->bottom, indicator); }
void VirtualItemView::finishRefresh(quint64 requestId, bool hasMore) { m_refreshController->complete(requestId, RefreshState::Refreshing, hasMore, true); }
void VirtualItemView::finishLoadMore(quint64 requestId, bool hasMore) { m_refreshController->complete(requestId, RefreshState::LoadingMore, hasMore, true); }
void VirtualItemView::finishLoadMore(quint64 requestId) { m_refreshController->complete(requestId, RefreshState::LoadingMore, true, true, true); }
quint64 VirtualItemView::beginRefresh()
{
    if (!model() || m_destroying) return 0;
    auto *controller = m_refreshController;
    if (controller->machine.state == RefreshState::Refreshing)
        return controller->machine.active;
    controller->clearGesture();
    controller->fillingGap = false;
    return controller->request(RefreshState::Refreshing, true);
}

void VirtualItemView::cancelRequest(quint64 requestId)
{
    auto *controller = m_refreshController;
    if (!controller->machine.cancelRequest(requestId)) return;
    controller->fillingGap = false;
    controller->notify();
}

void VirtualItemView::failRequest(quint64 requestId)
{
    if (m_refreshController->machine.finishExternalPage(requestId, false, false)) {
        m_refreshController->notify();
        return;
    }
    m_refreshController->complete(requestId, refreshState(), false, false);
}

quint64 VirtualItemView::beginExternalPage()
{
    if (!model()) return 0;
    auto *controller = m_refreshController;
    const quint64 id = controller->machine.beginExternalPage();
    controller->fillingGap = false;
    const QPointer<VirtualItemView> self(this);
    controller->cancel();
    return self && isRequestCurrent(id) ? id : 0;
}

bool VirtualItemView::isRequestCurrent(quint64 requestId) const
{
    return m_refreshController->machine.isCurrent(requestId);
}

void VirtualItemView::finishExternalPage(quint64 requestId, bool hasMore)
{
    if (!m_refreshController->machine.finishExternalPage(requestId, hasMore, true)) return;
    scheduleScrollUpdate();
    m_refreshController->notify();
}

VirtualItemView::LoadMorePauseReason VirtualItemView::loadMorePauseReason() const
{
    return m_refreshController->machine.pauseReason();
}

void VirtualItemView::retryLoadMore()
{
    if (!m_refreshController->machine.retryLoadMore()) return;
    scheduleScrollUpdate();
    m_refreshController->notify();
}

void VirtualItemView::setHasMore(bool hasMore)
{
    if (!m_refreshController->machine.setHasMore(hasMore)) return;
    m_refreshController->syncPresentation();
    scheduleScrollUpdate();
}

bool VirtualItemView::hasMoreAvailable() const
{
    return m_refreshController->machine.hasMore;
}

void VirtualItemView::evaluateScrollRefresh()
{
    scheduleScrollUpdate();
}

void VirtualItemView::setLoadMoreSuspended(bool suspended)
{
    auto &machine = m_refreshController->machine;
    if (machine.suspended == suspended) return;
    machine.suspended = suspended;
    if (!suspended) scheduleScrollUpdate();
    m_refreshController->notify();
}

bool VirtualItemView::loadMoreSuspended() const
{
    return m_refreshController->machine.suspended;
}

bool VirtualItemView::handleRefreshPointer(QMouseEvent *event, QWidget *childReceiver)
{
    auto *controller = m_refreshController;
    if (!controller || !controller->pullEnabled || (childReceiver && !controller->captured)) return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint localPoint = event->position().toPoint();
#else
    const QPoint localPoint = event->pos();
#endif
    const QPoint point = childReceiver ? childReceiver->mapTo(viewport(), localPoint) : localPoint;
    if (event->type() == QEvent::MouseButtonPress) {
        controller->tracking = event->modifiers() == Qt::NoModifier && controller->eligible(point);
        controller->origin = point;
        controller->captured = false;
        return controller->tracking;
    }
    if (!controller->tracking) return false;
    const QPoint delta = point - controller->origin;
    if (event->type() == QEvent::MouseMove && (event->buttons() & Qt::LeftButton)) {
        if (!controller->captured) {
            if (delta.manhattanLength() < QApplication::startDragDistance()) return false;
            if (delta.y() <= qAbs(delta.x()) || verticalOffset() != 0) {
                controller->tracking = false;
                scheduleScrollUpdate();
                return false;
            }
            controller->captured = true;
            m_deferPressSelection = false;
            m_pressedIndex = QPersistentModelIndex();
        }
        controller->machine.pull(delta.y());
        event->accept();
        controller->notify();
        return true;
    }
    if (event->type() != QEvent::MouseButtonRelease || event->button() != Qt::LeftButton) return false;
    const bool captured = controller->captured;
    controller->tracking = controller->captured = false;
    scheduleScrollUpdate();
    if (!captured) return false;
    event->accept();
    controller->machine.pull(delta.y());
    if (controller->machine.state == RefreshState::Armed) controller->request(RefreshState::Refreshing);
    else controller->cancel();
    return true;
}

bool VirtualItemView::handleRefreshWheel(QWheelEvent *event)
{
    auto *controller = m_refreshController;
    if (!controller || !controller->loadEnabled || controller->bottom.mode != IndicatorMode::Inline
        || !model() || !controller->machine.hasMore || controller->machine.pauseReason() != PauseReason::None || controller->tracking)
        return false;
    const auto state = refreshState();
    if (state != RefreshState::Idle && state != RefreshState::LoadingMore) return false;
    const QPoint pixel = event->pixelDelta();
    const qint64 travel = pixel.isNull() ? -qint64(event->angleDelta().y()) * wheelStepPixels() / 120 : -qint64(pixel.y());
    if (travel <= 0 || verticalOffset() < maximumVerticalOffset()) return false;
    if (state == RefreshState::Idle) {
        if (!controller->machine.canLoad(true, false)) return false;
        controller->request(RefreshState::LoadingMore);
    }
    event->accept();
    return true;
}

bool VirtualItemView::handleRefreshViewportEvent(QEvent *event)
{
    auto *controller = m_refreshController;
    switch (event->type()) {
    case QEvent::Show:
    case QEvent::Resize:
        scheduleScrollUpdate();
        return false;
    case QEvent::Hide:
    case QEvent::UngrabMouse:
    case QEvent::TouchCancel:
        if (controller->tracking || controller->viewportTouch || m_deferPressSelection)
            controller->cancel();
        return false;
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
        break;
    default: return false;
    }
    auto *touch = static_cast<QTouchEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto points = touch->points();
#else
    const auto points = touch->touchPoints();
#endif
    if (points.size() != 1) {
        const bool owned = controller->viewportTouch;
        if (owned || controller->tracking || m_deferPressSelection)
            controller->cancel();
        return owned;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint point = points.first().position().toPoint();
#else
    const QPoint point = points.first().pos().toPoint();
#endif
    if (event->type() == QEvent::TouchBegin) {
        if (!controller->eligible(point) || !passiveTouchTarget(viewport(), point)) return false;
        controller->viewportTouch = true;
    }
    if (!controller->viewportTouch) return false;
    const bool press = event->type() == QEvent::TouchBegin;
    const bool release = event->type() == QEvent::TouchEnd;
    const QPoint global = viewport()->mapToGlobal(point);
    QMouseEvent pointer(press ? QEvent::MouseButtonPress : release ? QEvent::MouseButtonRelease : QEvent::MouseMove,
                        point, global, press || release ? Qt::LeftButton : Qt::NoButton,
                        release ? Qt::NoButton : Qt::LeftButton, touch->modifiers());
    if (release) controller->viewportTouch = false;
    event->accept();
    QCoreApplication::sendEvent(viewport(), &pointer);
    return true;
}

} // namespace viv
