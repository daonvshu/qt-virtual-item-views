#include <virtualitemviews/virtualitemview.h>

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/layoutpolicy.h>
#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/widgetrecycler.h>

#include <QApplication>
#include <QAbstractItemView>
#include <QChildEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QCursor>
#include <QEasingCurve>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPair>
#include <QPointF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector>
#include <QWheelEvent>

#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

namespace viv {

// Out-of-line definition of a public constant: a *shared* build has to export a symbol
// for it. A consumer that odr-uses the member (QTest's QCOMPARE binds it to a const&)
// needs one, while an inline constexpr member that the library itself only reads as a
// constant expression never gets a definition emitted - MinGW/GCC then fails to link
// `__imp_...kLifecycleLogCapacity` in the consumer. Same pattern for the other public
// constants of exported classes (ScrollMapper, BlockSizeIndex, WidgetRecycler,
// HeaderGeometry, HeaderViewInterface).
// A shared build has to export the public constants of exported classes: a consumer that
// odr-uses one (QTest's QCOMPARE binds it to a const&) links against `__imp_...`, while
// the consumer's TU sees the member as dllimport and emits nothing. MinGW/GCC only emits
// the symbol in a TU that odr-uses it, so the library addresses it here. The same pattern
// lives next to the other public constants (ScrollMapper, BlockSizeIndex, WidgetRecycler,
// HeaderGeometry, HeaderViewInterface).
namespace {
[[maybe_unused]] const void *const volatile kExportedConstants[] = {
    &VirtualItemView::kLifecycleLogCapacity,
};
} // namespace

namespace {
/// Drop indicator of the kernel: a thin bar in the palette's highlight colour.
/// It is painted by itself instead of relying on autoFillBackground(), which
/// style sheet based styles ignore.
class DropIndicatorWidget : public QWidget
{
public:
    explicit DropIndicatorWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivDropIndicator"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    /// A line fills the whole rect; a frame draws a hollow rectangle ("drop into
    /// this item", the tree's OnItem indicator).
    void setFrame(bool frame)
    {
        if (m_frame == frame)
            return;
        m_frame = frame;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QColor color = palette().color(QPalette::Highlight);
        if (!m_frame) {
            painter.fillRect(rect(), color);
            return;
        }
        painter.setPen(color);
        const int thickness = qMin(2, qMin(width(), height()));
        for (int inset = 0; inset < thickness; ++inset) {
            const QRect outline = rect().adjusted(inset, inset, -inset - 1, -inset - 1);
            if (outline.isValid())
                painter.drawRect(outline);
        }
    }

private:
    bool m_frame = false;
};

/// Framework owned clipping container of the scrolling row pane (§31, row
/// direction). It paints nothing, so a business item widget keeps its own
/// background; Qt clips the children of a widget to its rect, which is what keeps
/// a row that scrolled behind the frozen band from showing through it.
class ItemPaneClipHost : public QWidget
{
public:
    explicit ItemPaneClipHost(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivItemPaneClipHost"));
        setFocusPolicy(Qt::NoFocus);
    }
};

class RowSpacingHost : public QWidget
{
public:
    explicit RowSpacingHost(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivRowSpacingHost"));
        setFocusPolicy(Qt::NoFocus);
        setProperty("vivShowSpacingLines", true);
    }

    void setContent(QWidget *content)
    {
        m_content = content;
        if (m_content) {
            QPointer<RowSpacingHost> host(this);
            QPointer<QWidget> child(m_content);
            child->setParent(this);
            if (!host || !child)
                return;
            child->show();
            if (!host)
                return;
        }
        layoutContent();
    }

    QWidget *content() const { return m_content; }
    void setLineColor(const QColor &color) { m_lineColor = color; update(); }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        layoutContent();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const int lineWidth = qMax(1, property("vivSpacingLineWidth").toInt());
        if (property("vivFillSpacingBackground").toBool())
            painter.fillRect(rect(), palette().brush(QPalette::Base));
        if (property("vivShowSpacingLines").toBool()) {
            const int left = qBound(0, property("vivSpacingLineLeftInset").toInt(), width());
            const int skipLeft = qBound(left, property("vivSpacingLineSkipX").toInt(), width());
            const int skipRight = qBound(skipLeft,
                skipLeft + qMax(0, property("vivSpacingLineSkipWidth").toInt()), width());
            for (int y : {0, height() - lineWidth}) {
                painter.fillRect(QRect(left, y, skipLeft - left, lineWidth), m_lineColor);
                painter.fillRect(QRect(skipRight, y, width() - skipRight, lineWidth), m_lineColor);
            }
        }
    }

private:
    void layoutContent()
    {
        if (m_content)
            m_content->setGeometry(0, qMax(1, property("vivSpacingLineWidth").toInt()), width(),
                                   qMax(0, height() - 2 * qMax(1, property("vivSpacingLineWidth").toInt())));
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::DynamicPropertyChange) {
            layoutContent();
            update();
        }
        return QWidget::event(event);
    }

    QPointer<QWidget> m_content;
    QColor m_lineColor;
};

/// The line between two row panes (§31). The item widgets cover the viewport, so a
/// line painted by the viewport itself would be hidden behind them; this overlay
/// sits above them and lets input through.
class ItemPaneSeparatorLine : public QWidget
{
public:
    explicit ItemPaneSeparatorLine(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vivItemPaneSeparatorLine"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFocusPolicy(Qt::NoFocus);
    }

    void setSeparator(const PaneSeparatorStyle &style, const QColor &styleSeparatorColor)
    {
        m_style = style;
        m_resolvedColor = style.effectiveColor(styleSeparatorColor);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_style.isVisible() || m_resolvedColor.alpha() == 0)
            return;
        QPainter painter(this);
        if (m_style.lineStyle == Qt::SolidLine) {
            painter.fillRect(rect(), m_resolvedColor);
            return;
        }
        QPen pen(m_resolvedColor);
        pen.setStyle(m_style.lineStyle);
        pen.setWidth(1);
        painter.setPen(pen);
        const int y = rect().top() + (qMax(0, rect().height() - 1)) / 2;
        painter.drawLine(rect().left(), y, rect().right(), y);
    }

private:
    PaneSeparatorStyle m_style;
    QColor m_resolvedColor;
};
} // namespace

namespace {
constexpr int kDefaultOverscan = 2;
constexpr int kDefaultWheelScrollItems = 3;
/// Automatic height measurement must converge: a widget whose size hint depends
/// on its own geometry could otherwise trigger an endless relayout loop.
constexpr int kMaxConsecutiveMeasurePasses = 8;

/// QMouseEvent::position() only exists in Qt 6; Qt 5 delivers QPoint.
inline QPoint mousePosition(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

/// Same for the drag/drop events (QDropEvent carries the position the same way).
inline QPoint dropPosition(const QDropEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}
} // namespace

VirtualItemView::VirtualItemView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    // v0.1 lists fill the viewport width; horizontal virtualization arrives
    // with VirtualTableView.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setAutoFillBackground(false);
    viewport()->setAttribute(Qt::WA_Hover);
    viewport()->setMouseTracking(true);
    viewport()->installEventFilter(this);

    m_overscanBefore = kDefaultOverscan;
    m_overscanAfter = kDefaultOverscan;
    m_wheelScrollItems = kDefaultWheelScrollItems;

    m_recycler = new WidgetRecycler(viewport());
    m_recycler->setFactory([this](WidgetType type, QWidget *parentWidget) {
        return m_adapter ? m_adapter->createWidget(type, parentWidget) : nullptr;
    });
}

VirtualItemView::~VirtualItemView()
{
    m_destroying = true;
    viewport()->removeEventFilter(this);
    for (QWidget *widget : m_rowSpacingWidgets)
        delete widget;
    for (const QVector<QWidget *> &pool : m_rowSpacingPool)
        qDeleteAll(pool);
    // Materialized widgets have to be released while the adapter and the
    // recycler are both alive: business code stops its timers / async requests
    // in unbindWidget(), and a pooled widget must not outlive the factory that
    // created it. Only then may the owned collaborators go away.
    recycleAllItems();
    if (m_recycler)
        m_recycler->clear();
    disconnectModel(m_model.data());
    retireOwnedSelectionModel();
    if (m_ownAdapter) {
        delete m_adapter;
        m_adapter = nullptr;
        m_ownAdapter = false;
    }
    if (m_ownLayout) {
        delete m_layout;
        m_layout = nullptr;
        m_ownLayout = false;
    }
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

void VirtualItemView::setModel(QAbstractItemModel *model)
{
    if (m_model.data() == model)
        return;
    const quint64 changeSerial = ++m_modelChangeSerial;
    const QPointer<QAbstractItemModel> requestedModel(model);

    disconnectModel(m_model.data());
    // A drag or drop in flight refers to the outgoing model (§38).
    finishDrag();
    if (m_modelChangeSerial != changeSerial)
        return;
    clearVisualTransitions();
    if (m_selectionModel)
        disconnect(m_selectionModel.data(), nullptr, this, nullptr);
    m_hoveredIndex = QPersistentModelIndex();
    recycleAllItems();
    if (m_modelChangeSerial != changeSerial)
        return;
    model = requestedModel.data();
    m_explicitPinned.clear();
    cancelPendingAnchor();
    m_scrollOffset = 0;
    m_scrollMapper.resetAnchor();

    m_model = model;
    connectModel(model);

    // Invariant: selectionModel()->model() == model(), or there is no selection
    // model at all. A selection model of the outgoing model cannot address the
    // new one, so it is detached (an external one is not deleted).
    if (m_ownSelectionModel) {
        retireOwnedSelectionModel();
        if (m_modelChangeSerial != changeSerial)
            return;
    } else if (m_selectionModel && m_selectionModel->model() != model) {
        m_selectionModel = nullptr;
    }
    if (!m_selectionModel && model) {
        m_selectionModel = new QItemSelectionModel(model, this);
        m_ownSelectionModel = true;
    }
    connectSelectionModel();
    emit selectionModelChanged(m_selectionModel.data());
    if (m_modelChangeSerial != changeSerial)
        return;

    resetLayoutForNewModel();
    if (m_modelChangeSerial != changeSerial)
        return;
    relayout();
}

void VirtualItemView::connectModel(QAbstractItemModel *model)
{
    if (!model)
        return;

    connect(model, &QAbstractItemModel::dataChanged, this, &VirtualItemView::onDataChanged);
    connect(model, &QAbstractItemModel::rowsAboutToBeInserted,
            this, &VirtualItemView::onRowsAboutToBeInserted);
    connect(model, &QAbstractItemModel::rowsInserted, this, &VirtualItemView::onRowsInserted);
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved,
            this, &VirtualItemView::onRowsAboutToBeRemoved);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &VirtualItemView::onRowsRemoved);
    connect(model, &QAbstractItemModel::rowsAboutToBeMoved,
            this, &VirtualItemView::onRowsAboutToBeMoved);
    connect(model, &QAbstractItemModel::rowsMoved, this, &VirtualItemView::onRowsMoved);
    connect(model, &QAbstractItemModel::layoutAboutToBeChanged,
            this, &VirtualItemView::onLayoutAboutToBeChanged);
    connect(model, &QAbstractItemModel::layoutChanged, this, &VirtualItemView::onLayoutChanged);
    connect(model, &QAbstractItemModel::modelAboutToBeReset,
            this, &VirtualItemView::onModelAboutToBeReset);
    connect(model, &QAbstractItemModel::modelReset, this, &VirtualItemView::onModelReset);
    // The materialized identity of a row is the (row, 0) cell (see viewIndex()), and a column
    // change that touches column 0 renames or invalidates exactly that cell. Every materialized
    // widget is released *before* the change, while the index it was bound to is still valid -
    // the adapter sees the unbind it is promised, and the widget goes back into the pool
    // instead of staying bound to a cell that no longer exists. The next pass materializes the
    // canonical (row, 0) cells again and recomputes WidgetType for them (P1 of the fourth
    // review; the earlier "re-key the live items" route skipped the unbind, the type and the
    // index lookup). A change that does not touch column 0 cannot affect any row identity.
    connect(model, &QAbstractItemModel::columnsAboutToBeInserted, this,
            [this](const QModelIndex &parent, int first, int) {
                if (!managesVisibleRows() && !parent.isValid() && first == 0)
                    recycleItemsForColumnChange();
            });
    connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, this,
            [this](const QModelIndex &parent, int first, int) {
                if (!managesVisibleRows() && !parent.isValid() && first == 0)
                    recycleItemsForColumnChange();
            });
    connect(model, &QAbstractItemModel::columnsAboutToBeMoved, this,
            [this](const QModelIndex &parent, int start, int, const QModelIndex &destination,
                   int destinationColumn) {
                if (!managesVisibleRows() && !parent.isValid() && !destination.isValid()
                    && (start == 0 || destinationColumn == 0)) {
                    recycleItemsForColumnChange();
                }
            });
    const auto restoreIdentity = [this](const QModelIndex &) {
        restoreItemsAfterColumnChange();
    };
    connect(model, &QAbstractItemModel::columnsInserted, this, restoreIdentity);
    connect(model, &QAbstractItemModel::columnsRemoved, this, restoreIdentity);
    connect(model, &QAbstractItemModel::columnsMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) {
                restoreItemsAfterColumnChange();
            });
}

void VirtualItemView::disconnectModel(QAbstractItemModel *model)
{
    if (!model)
        return;
    disconnect(model, nullptr, this, nullptr);
}

void VirtualItemView::resetLayoutForNewModel()
{
    if (!m_layout)
        return;
    const qsizetype count = viewItemCount();
    m_layout->resetItems(count, estimateItemSize(count > 0 ? count - 1 : 0));
    applyRowSpacingOverrides();
}

void VirtualItemView::applyRowSpacingOverrides()
{
}

void VirtualItemView::configureRowSpacingWidget(QWidget *widget) const
{
    Q_UNUSED(widget);
}

QRegion VirtualItemView::rowSpacingWidgetMask(qsizetype row, const QRect &geometry) const
{
    Q_UNUSED(row);
    return QRegion(QRect(QPoint(), geometry.size()));
}

void VirtualItemView::updateRowSpacingWidgetMasks()
{
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 spacingSerial = m_rowSpacingStateSerial;
    QHash<qsizetype, QPointer<QWidget>> widgets;
    for (auto it = m_rowSpacingWidgets.cbegin(); it != m_rowSpacingWidgets.cend(); ++it)
        widgets.insert(it.key(), it.value());
    for (auto it = widgets.cbegin(); it != widgets.cend(); ++it) {
        if (modelChangeSerial() != modelSerial || viewMappingSerial() != mappingSerial
            || m_rowSpacingStateSerial != spacingSerial) {
            abortMaterializationPass();
            return;
        }
        const QPointer<QWidget> widget = it.value();
        if (!widget || m_rowSpacingWidgets.value(it.key()) != widget.data())
            continue;
        const QRect geometry(widget->mapTo(viewport(), QPoint()), widget->size());
        const QRegion mask = rowSpacingWidgetMask(it.key(), geometry);
        if (!widget || modelChangeSerial() != modelSerial || viewMappingSerial() != mappingSerial
            || m_rowSpacingStateSerial != spacingSerial) {
            abortMaterializationPass();
            return;
        }
        if (mask == QRegion(widget->rect()))
            widget->clearMask();
        else
            widget->setMask(mask);
        widget->setVisible(!mask.isEmpty());
    }
}

void VirtualItemView::raiseRowSpacingWidgets() const
{
    for (QWidget *widget : m_rowSpacingWidgets)
        widget->raise();
}

QVector<QRect> VirtualItemView::rowSpacingWidgetRectsInView() const
{
    QVector<QRect> rects;
    rects.reserve(m_rowSpacingWidgets.size());
    for (QWidget *widget : m_rowSpacingWidgets) {
        if (widget->isVisible())
            rects.append(QRect(widget->mapTo(const_cast<VirtualItemView *>(this), QPoint()),
                               widget->size()));
    }
    return rects;
}

void VirtualItemView::setRowSpacing(int pixels)
{
    const int spacing = qMax(0, pixels);
    if (m_rowSpacing == spacing)
        return;
    ++m_rowSpacingStateSerial;
    m_rowSpacing = spacing;
    if (auto *layout = dynamic_cast<ListLayout *>(m_layout)) {
        layout->setItemSpacing(spacing);
        applyRowSpacingOverrides();
    }
    relayout();
}

void VirtualItemView::setRowSpacingFactory(RowSpacingFactory factory, RowSpacingBinder binder)
{
    const quint64 changeSerial = ++m_rowSpacingStateSerial;
    QHash<qsizetype, QWidget *> active;
    QHash<int, QVector<QWidget *>> pooled;
    active.swap(m_rowSpacingWidgets);
    pooled.swap(m_rowSpacingPool);
    QVector<QPointer<QWidget>> oldWidgets;
    for (QWidget *widget : active)
        oldWidgets.append(widget);
    for (const QVector<QWidget *> &pool : pooled) {
        for (QWidget *widget : pool)
            oldWidgets.append(widget);
    }
    for (const QPointer<QWidget> &widget : oldWidgets)
        delete widget.data();
    if (m_rowSpacingStateSerial != changeSerial)
        return;
    m_rowSpacingFactory = std::move(factory);
    m_rowSpacingBinder = std::move(binder);
    relayout();
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void VirtualItemView::setSelectionModel(QItemSelectionModel *selectionModel)
{
    if (m_selectionModel.data() == selectionModel)
        return;
    if (selectionModel && m_model && selectionModel->model() != m_model.data()) {
        qWarning("VirtualItemView::setSelectionModel(): the selection model belongs to another "
                 "model; the current selection model is kept");
        return;
    }
    if (m_selectionModel)
        disconnect(m_selectionModel.data(), nullptr, this, nullptr);
    retireOwnedSelectionModel();
    m_selectionModel = selectionModel;
    connectSelectionModel();
    emit selectionModelChanged(m_selectionModel.data());
    refreshVisualStates();
}

void VirtualItemView::retireOwnedSelectionModel()
{
    if (!m_ownSelectionModel)
        return;
    QItemSelectionModel *previous = m_selectionModel.data();
    m_selectionModel = nullptr;
    m_ownSelectionModel = false;
    if (!previous)
        return;
    // Qt may still be delivering current/selection notifications from this object.
    disconnect(previous, nullptr, this, nullptr);
    previous->setParent(nullptr);
    previous->deleteLater();
}

void VirtualItemView::connectSelectionModel()
{
    if (!m_selectionModel)
        return;
    connect(m_selectionModel.data(), &QItemSelectionModel::selectionChanged, this,
            [this]() { refreshVisualStates(); });
}

VirtualItemView::VisualState VirtualItemView::visualState(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != m_model)
        return {};
    return animatedVisualState(index, m_hoveredIndex == index,
                               m_selectionModel && m_selectionModel->isSelected(index));
}

VirtualItemView::VisualState VirtualItemView::animatedVisualState(
    const QModelIndex &index, bool hovered, bool selected) const
{
    const qreal hoverTarget = hovered ? 1.0 : 0.0;
    const qreal selectedTarget = selected ? 1.0 : 0.0;
    VisualState state{hovered, selected, hoverTarget, selectedTarget};
    if (m_visualStateAnimationDuration <= 0)
        return state;

    const QPersistentModelIndex key(index);
    auto it = m_visualTransitions.find(key);
    if (it == m_visualTransitions.end()) {
        // Public queries may name offscreen indexes; cap retained snapshots.
        if (m_visualTransitions.size() >= 8192) {
            const_cast<VirtualItemView *>(this)->stopVisualTransition(
                m_visualTransitions.begin().value());
            m_visualTransitions.erase(m_visualTransitions.begin());
        }
        m_visualTransitions.insert(key, {hoverTarget, selectedTarget, hovered, selected, nullptr});
        return state;
    }

    VisualTransition &transition = it.value();
    state.hoverProgress = transition.hoverProgress;
    state.selectedProgress = transition.selectedProgress;
    if (transition.hoverTarget != hovered || transition.selectedTarget != selected) {
        transition.hoverTarget = hovered;
        transition.selectedTarget = selected;
        auto *view = const_cast<VirtualItemView *>(this);
        if (!transition.animation) {
            auto *animation = new QVariantAnimation(view);
            transition.animation = animation;
            connect(animation, &QVariantAnimation::valueChanged, view,
                    [view, key, animation](const QVariant &value) {
                auto current = view->m_visualTransitions.find(key);
                if (current == view->m_visualTransitions.end()
                    || current.value().animation != animation)
                    return;
                const QPointF progress = value.toPointF();
                current.value().hoverProgress = progress.x();
                current.value().selectedProgress = progress.y();
                view->scheduleVisualStateRefresh(key);
            });
            connect(animation, &QVariantAnimation::finished, view,
                    [view, key, animation]() {
                auto current = view->m_visualTransitions.find(key);
                if (current == view->m_visualTransitions.end()
                    || current.value().animation != animation)
                    return;
                current.value().hoverProgress = qreal(current.value().hoverTarget);
                current.value().selectedProgress = qreal(current.value().selectedTarget);
                current.value().animation = nullptr;
                animation->deleteLater();
                view->scheduleVisualStateRefresh(key);
            });
        }
        QVariantAnimation *animation = transition.animation;
        animation->stop();
        animation->setDuration(m_visualStateAnimationDuration);
        animation->setEasingCurve(QEasingCurve::InOutCubic);
        animation->setStartValue(QPointF(state.hoverProgress, state.selectedProgress));
        animation->setEndValue(QPointF(hoverTarget, selectedTarget));
        animation->start();
    }
    return state;
}

void VirtualItemView::scheduleVisualStateRefresh(const QPersistentModelIndex &index)
{
    m_pendingVisualRefreshes.insert(index);
    if (m_visualRefreshScheduled)
        return;
    m_visualRefreshScheduled = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_visualRefreshScheduled = false;
        QSet<QPersistentModelIndex> pending;
        pending.swap(m_pendingVisualRefreshes);
        for (const QPersistentModelIndex &index : pending) {
            if (index.isValid() && index.model() == m_model)
                refreshVisualState(QModelIndex(index));
        }
    }, Qt::QueuedConnection);
}

void VirtualItemView::stopVisualTransition(VisualTransition &transition)
{
    if (!transition.animation)
        return;
    disconnect(transition.animation, nullptr, this, nullptr);
    transition.animation->stop();
    transition.animation->deleteLater();
    transition.animation = nullptr;
}

void VirtualItemView::setVisualStateAnimationDuration(int milliseconds)
{
    milliseconds = qMax(0, milliseconds);
    if (m_visualStateAnimationDuration == milliseconds)
        return;
    m_visualStateAnimationDuration = milliseconds;
    clearVisualTransitions();
    refreshVisualStates();
}

void VirtualItemView::clearVisualTransition(const QModelIndex &index)
{
    auto it = m_visualTransitions.find(QPersistentModelIndex(index));
    if (it == m_visualTransitions.end())
        return;
    stopVisualTransition(it.value());
    m_visualTransitions.erase(it);
    m_pendingVisualRefreshes.remove(QPersistentModelIndex(index));
}

void VirtualItemView::clearVisualTransitionsForRow(const QModelIndex &index)
{
    for (auto it = m_visualTransitions.begin(); it != m_visualTransitions.end();) {
        const QModelIndex key = it.key();
        if (key.isValid() && key.parent() == index.parent() && key.row() == index.row()) {
            stopVisualTransition(it.value());
            m_pendingVisualRefreshes.remove(it.key());
            it = m_visualTransitions.erase(it);
        } else {
            ++it;
        }
    }
}

void VirtualItemView::clearVisualTransitions()
{
    for (auto it = m_visualTransitions.begin(); it != m_visualTransitions.end(); ++it)
        stopVisualTransition(it.value());
    m_visualTransitions.clear();
    m_pendingVisualRefreshes.clear();
}

void VirtualItemView::rekeyPersistentState()
{
    QSet<QPersistentModelIndex> pinned;
    for (const QPersistentModelIndex &index : m_explicitPinned) {
        const QModelIndex cell = index;
        if (isPersistentRowStateValid(index)
            || (!usesItemWidgets() && index.isValid()
                && isPersistentRowStateValid(cell.siblingAtColumn(0))))
            pinned.insert(index);
    }
    m_explicitPinned.swap(pinned);

    QHash<QPersistentModelIndex, VisualTransition> transitions;
    for (auto it = m_visualTransitions.begin(); it != m_visualTransitions.end(); ++it) {
        if (it.key().isValid())
            transitions.insert(it.key(), it.value());
        else
            stopVisualTransition(it.value());
    }
    m_visualTransitions.swap(transitions);

    QSet<QPersistentModelIndex> pending;
    for (const QPersistentModelIndex &index : m_pendingVisualRefreshes) {
        if (index.isValid())
            pending.insert(index);
    }
    m_pendingVisualRefreshes.swap(pending);
    rebuildLookup();
}

bool VirtualItemView::isPersistentRowStateValid(const QModelIndex &index) const
{
    return index.isValid();
}

void VirtualItemView::setHoverBackgroundColor(const QColor &color)
{
    if (!color.isValid() || m_hoverBackgroundColor == color)
        return;
    m_hoverBackgroundColor = color;
    refreshVisualStates();
}

void VirtualItemView::setSelectedBackgroundColor(const QColor &color)
{
    if (!color.isValid() || m_selectedBackgroundColor == color)
        return;
    m_selectedBackgroundColor = color;
    refreshVisualStates();
}

void VirtualItemView::updateHoveredIndex(const QModelIndex &index)
{
    if (m_hoveredIndex == index)
        return;
    const QPersistentModelIndex previous = m_hoveredIndex;
    m_hoveredIndex = QPersistentModelIndex(index);
    if (previous.isValid())
        refreshVisualState(QModelIndex(previous));
    if (m_hoveredIndex.isValid())
        refreshVisualState(QModelIndex(m_hoveredIndex));
}

void VirtualItemView::refreshHoveredIndex()
{
    QWidget *target = QApplication::widgetAt(QCursor::pos());
    if (target != viewport() && !viewport()->isAncestorOf(target)) {
        updateHoveredIndex(QModelIndex());
        return;
    }
    const QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
    updateHoveredIndex(viewport()->rect().contains(pos) ? indexAt(pos) : QModelIndex());
}

void VirtualItemView::refreshVisualStates()
{
    if (!m_adapter)
        return;
    const quint64 modelSerial = m_modelChangeSerial;
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 lifecycleSerial = m_itemLifecycleSerial;
    const QPointer<QAbstractItemModel> activeModel(m_model);
    WidgetAdapter *const activeAdapter = m_adapter;
    QList<QPersistentModelIndex> indexes;
    indexes.reserve(m_items.size());
    for (const MaterializedItem &item : m_items)
        indexes.append(item.index);
    for (const QPersistentModelIndex &persistent : indexes) {
        if (!activeModel || m_model != activeModel.data()
            || m_modelChangeSerial != modelSerial || viewMappingSerial() != mappingSerial
            || m_itemLifecycleSerial != lifecycleSerial || m_adapter != activeAdapter)
            return;
        const QModelIndex index = persistent;
        QPointer<QWidget> widget(index.isValid() ? widgetForIndex(index) : nullptr);
        if (widget)
            activeAdapter->visualStateChanged(widget.data(), index);
    }
}

void VirtualItemView::refreshVisualState(const QModelIndex &index)
{
    if (!m_adapter || !index.isValid() || index.model() != m_model)
        return;
    const QPersistentModelIndex target(index);
    const quint64 modelSerial = m_modelChangeSerial;
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 lifecycleSerial = m_itemLifecycleSerial;
    const QPointer<QAbstractItemModel> activeModel(m_model);
    WidgetAdapter *const activeAdapter = m_adapter;
    QList<QPersistentModelIndex> indexes;
    indexes.reserve(m_items.size());
    for (const MaterializedItem &item : m_items)
        indexes.append(item.index);
    for (const QPersistentModelIndex &persistent : indexes) {
        if (!activeModel || m_model != activeModel.data()
            || m_modelChangeSerial != modelSerial || viewMappingSerial() != mappingSerial
            || m_itemLifecycleSerial != lifecycleSerial || m_adapter != activeAdapter)
            return;
        const QModelIndex materialized = persistent;
        if (!target.isValid() || !materialized.isValid()
            || materialized.parent() != target.parent()
            || materialized.row() != target.row())
            continue;
        QPointer<QWidget> widget(widgetForIndex(materialized));
        if (widget)
            activeAdapter->visualStateChanged(widget.data(), materialized);
    }
}

void VirtualItemView::prepareHoverTracking(QWidget *widget)
{
    if (!widget)
        return;
    widget->setMouseTracking(true);
    widget->setAttribute(Qt::WA_Hover);
    widget->installEventFilter(this);
    for (QWidget *child : widget->findChildren<QWidget *>()) {
        child->setMouseTracking(true);
        child->setAttribute(Qt::WA_Hover);
        child->installEventFilter(this);
    }
}

bool VirtualItemView::eventFilter(QObject *watched, QEvent *event)
{
    if (m_destroying)
        return false;
    auto *widget = qobject_cast<QWidget *>(watched);
    if (widget && event->type() == QEvent::ChildAdded) {
        const QPointer<QObject> child(static_cast<QChildEvent *>(event)->child());
        QTimer::singleShot(0, this, [this, child]() {
            auto *added = qobject_cast<QWidget *>(child.data());
            if (added && (added == viewport() || viewport()->isAncestorOf(added)))
                prepareHoverTracking(added);
        });
    }
    if (widget && (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove
                   || event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        const QPoint pos = event->type() == QEvent::MouseMove
            ? widget->mapTo(viewport(), mousePosition(static_cast<QMouseEvent *>(event)))
            : viewport()->mapFromGlobal(QCursor::pos());
        updateHoveredIndex(viewport()->rect().contains(pos) ? indexAt(pos) : QModelIndex());
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

QModelIndex VirtualItemView::currentIndex() const
{
    return m_selectionModel ? m_selectionModel->currentIndex() : QModelIndex();
}

void VirtualItemView::setSelectionMode(SelectionMode mode)
{
    if (m_selectionMode == mode)
        return;
    m_selectionMode = mode;
    if (!m_selectionModel)
        return;

    switch (mode) {
    case SelectionMode::NoSelection:
        m_selectionModel->clearSelection();
        break;
    case SelectionMode::SingleSelection: {
        // Keep only the current index selected.
        const QModelIndex current = m_selectionModel->currentIndex();
        m_selectionModel->clearSelection();
        if (current.isValid())
            m_selectionModel->select(selectionRange(current, current),
                                     selectionFlagsFor(QItemSelectionModel::ClearAndSelect));
        break;
    }
    case SelectionMode::MultiSelection:
    case SelectionMode::ExtendedSelection:
        break;
    }
}

void VirtualItemView::setSelectionBehavior(SelectionBehavior behavior)
{
    m_selectionBehavior = behavior;
}

void VirtualItemView::setCurrentIndex(const QModelIndex &index)
{
    if (!m_selectionModel)
        return;
    const QPointer<VirtualItemView> self(this);
    const QPointer<QItemSelectionModel> selection = m_selectionModel;
    const quint64 modelSerial = modelChangeSerial();
    if (!index.isValid()) {
        selection->clearCurrentIndex();
        if (!self || selection != m_selectionModel || modelChangeSerial() != modelSerial)
            return;
        m_selectionAnchor = QPersistentModelIndex();
        return;
    }
    const QPersistentModelIndex target(index);
    switch (m_selectionMode) {
    case SelectionMode::NoSelection:
        m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current);
        break;
    default:
        selectCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        if (!self || selection != m_selectionModel || modelChangeSerial() != modelSerial
            || !target.isValid() || target.model() != model())
            return;
        pinCurrentIndex(target);
        if (!self || selection != m_selectionModel || modelChangeSerial() != modelSerial
            || !target.isValid() || target.model() != model())
            return;
        m_selectionAnchor = target;
        break;
    }
}

void VirtualItemView::activateIndex(const QModelIndex &index)
{
    if (!index.isValid() || index.model() != model())
        return;
    const QPointer<VirtualItemView> self(this);
    const QPersistentModelIndex target(index);
    const quint64 modelSerial = modelChangeSerial();
    setCurrentIndex(index);
    if (!self || modelChangeSerial() != modelSerial || !target.isValid()
        || target.model() != model())
        return;
    emit clicked(target);
    if (!self || modelChangeSerial() != modelSerial || !target.isValid()
        || target.model() != model())
        return;
    emit activated(target);
}

void VirtualItemView::pinCurrentIndex(const QModelIndex &index)
{
    if (!m_selectionModel || !index.isValid())
        return;
    // QItemSelectionModel::select() with the Rows/Columns flag selects the whole
    // row/column and moves the current index to its first cell; re-assert the
    // current cell so that cell navigation keeps working.
    if (rowFlags() != QItemSelectionModel::NoUpdate)
        m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current);
}

void VirtualItemView::selectCurrentIndex(const QModelIndex &index,
                                         QItemSelectionModel::SelectionFlags command)
{
    if (m_selectionBehavior == SelectionBehavior::SelectRows && usesExplicitRowSelection()) {
        const QPointer<VirtualItemView> self(this);
        const QPointer<QItemSelectionModel> selection = m_selectionModel;
        const QPersistentModelIndex current(index);
        selection->setCurrentIndex(index, QItemSelectionModel::Current);
        if (!self || !selection || selection != m_selectionModel || !current.isValid()
            || current.model() != model())
            return;
        selection->select(selectionRange(current, current), command);
    } else {
        m_selectionModel->setCurrentIndex(index, command | rowFlags());
    }
}

QItemSelectionModel::SelectionFlags VirtualItemView::rowFlags() const
{
    return m_selectionBehavior == SelectionBehavior::SelectRows && !usesExplicitRowSelection()
        ? QItemSelectionModel::Rows : QItemSelectionModel::NoUpdate;
}

QItemSelectionModel::SelectionFlags
VirtualItemView::selectionFlagsFor(QItemSelectionModel::SelectionFlags command) const
{
    if (m_selectionMode == SelectionMode::NoSelection)
        return QItemSelectionModel::NoUpdate;
    return command | rowFlags();
}

void VirtualItemView::appendLifecycleLog(const QString &entry)
{
    if (!m_lifecycleLogEnabled)
        return;
    m_lifecycleLog.append(entry);
    while (m_lifecycleLog.size() > kLifecycleLogCapacity)
        m_lifecycleLog.removeFirst();
}

void VirtualItemView::setLifecycleLoggingEnabled(bool enabled)
{
    m_lifecycleLogEnabled = enabled;
    if (!enabled)
        m_lifecycleLog.clear();
}

void VirtualItemView::clearLifecycleLog()
{
    m_lifecycleLog.clear();
}

// ---------------------------------------------------------------------------
// Adapter / layout
// ---------------------------------------------------------------------------

void VirtualItemView::setAdapter(WidgetAdapter *adapter, bool takeOwnership)
{
    installAdapter(adapter, takeOwnership, true);
}

bool VirtualItemView::installAdapter(WidgetAdapter *adapter, bool takeOwnership, bool relayoutAfter)
{
    if (m_adapter == adapter) {
        m_ownAdapter = m_ownAdapter || takeOwnership;
        return true;
    }
    const quint64 changeSerial = ++m_adapterChangeSerial;
    const quint64 modelSerial = m_modelChangeSerial;
    const quint64 mappingSerial = viewMappingSerial();
    WidgetAdapter *const previous = m_adapter;
    // Order matters: hand the materialized widgets back through the *old*
    // adapter, drop every pooled widget (a widget class of another adapter must
    // never be handed out through the new adapter's WidgetType namespace), and
    // only then let the old adapter die.
    recycleAllItems();
    if (m_adapterChangeSerial != changeSerial || m_modelChangeSerial != modelSerial
        || viewMappingSerial() != mappingSerial || m_adapter != previous)
        return false;
    if (m_recycler)
        m_recycler->clear();
    if (m_adapterChangeSerial != changeSerial || m_modelChangeSerial != modelSerial
        || viewMappingSerial() != mappingSerial || m_adapter != previous)
        return false;
    if (m_ownAdapter) {
        m_adapter = nullptr;
        m_ownAdapter = false;
        delete previous;
        if (m_adapterChangeSerial != changeSerial || m_modelChangeSerial != modelSerial
            || viewMappingSerial() != mappingSerial || m_adapter)
            return false;
    }
    m_adapter = adapter;
    m_ownAdapter = takeOwnership;
    if (relayoutAfter)
        relayout();
    return true;
}

void VirtualItemView::setLayoutPolicy(LayoutPolicy *policy, bool takeOwnership)
{
    if (m_layout == policy) {
        m_ownLayout = m_ownLayout || takeOwnership;
        return;
    }
    recycleAllItems();
    if (m_ownLayout)
        delete m_layout;
    m_layout = policy;
    m_ownLayout = policy ? takeOwnership : false;
    if (m_layout) {
        m_layout->setCrossExtent(viewport()->width());
        rebuildSizeIndex();
    }
    relayout();
}

// ---------------------------------------------------------------------------
// Item size model (shared by the List and the Table kernel)
// ---------------------------------------------------------------------------

void VirtualItemView::setItemHeightMode(ItemHeightMode mode)
{
    if (m_heightMode == mode)
        return;
    m_heightMode = mode;
    m_measurePasses = 0;
    rebuildSizeIndex();
    relayout();
}

void VirtualItemView::setUniformItemHeight(int height)
{
    const int clamped = qMax(0, height);
    if (m_uniformItemHeight == clamped && m_heightMode == ItemHeightMode::Uniform)
        return;
    m_uniformItemHeight = clamped;
    m_heightMode = ItemHeightMode::Uniform;
    m_measurePasses = 0;
    rebuildSizeIndex();
    relayout();
}

void VirtualItemView::setEstimatedItemHeight(int height)
{
    const int clamped = qMax(1, height);
    if (m_estimatedItemHeight == clamped)
        return;
    m_estimatedItemHeight = clamped;
    if (m_heightMode == ItemHeightMode::Variable) {
        rebuildSizeIndex();
        relayout();
    }
}

void VirtualItemView::setAutoMeasureItemHeight(bool enabled)
{
    if (m_autoMeasure == enabled)
        return;
    m_autoMeasure = enabled;
    m_measurePasses = 0;
    markDirty();
}

void VirtualItemView::rebuildSizeIndex()
{
    if (!m_layout)
        return;

    if (m_heightMode == ItemHeightMode::Uniform)
        m_layout->setSizeIndex(new FixedSizeIndex(0, m_uniformItemHeight), true);
    else
        m_layout->setSizeIndex(new BlockSizeIndex(0, m_estimatedItemHeight), true);

    resetLayoutForNewModel();
}

int VirtualItemView::estimateItemSize(qsizetype item) const
{
    if (m_heightMode == ItemHeightMode::Uniform)
        return m_uniformItemHeight;

    const QModelIndex index = viewIndex(item);
    if (index.isValid() && m_adapter) {
        const QSize hint = m_adapter->estimatedSize(index);
        if (hint.height() > 0)
            return hint.height();
    }
    return m_estimatedItemHeight;
}

int VirtualItemView::measuredHeightOf(const MaterializedItem &item) const
{
    if (!item.widget)
        return 0;
    const int width = item.geometry.width() > 0 ? item.geometry.width() : viewport()->width();
    if (item.widget->hasHeightForWidth())
        return item.widget->heightForWidth(width);
    return item.widget->sizeHint().height();
}

void VirtualItemView::afterMaterialize()
{
    if (!m_autoMeasure || !m_layout || materializedItems().isEmpty())
        return;

    if (m_heightMode == ItemHeightMode::Uniform) {
        if (m_uniformItemHeight > 0)
            return;
        // "Fit to first item" mode: measure once and switch to that height.
        const int measured = measuredHeightOf(materializedItems().first());
        if (measured <= 0)
            return;
        // Captured before the size changes: afterwards the same pixel offset can
        // belong to a different item.
        const ScrollAnchor anchor = captureAnchor();
        m_uniformItemHeight = measured;
        m_layout->setItemSize(0, measured);
        setPendingAnchor(anchor);
        markDirty();
        return;
    }

    // The anchor has to describe what the user is looking at *now*: measuring
    // changes the offsets, so taking it after the loop would anchor whatever item
    // ends up at the old pixel offset (P1-2).
    const ScrollAnchor anchor = captureAnchor();

    bool changed = false;
    for (const MaterializedItem &item : materializedItems()) {
        const qsizetype row = viewItemForIndex(item.index);
        if (row < 0 || !canMeasureItem(row))
            continue;
        const int measured = measuredHeightOf(item);
        if (measured <= 0 || measured == m_layout->itemSize(row))
            continue;
        m_layout->setItemSize(row, measured);
        changed = true;
    }

    if (!changed) {
        m_measurePasses = 0;
        return;
    }
    if (++m_measurePasses > kMaxConsecutiveMeasurePasses) {
        // Give up on this oscillation and keep the last measured heights.
        m_measurePasses = 0;
        return;
    }
    // Heights above the viewport changed: keep the top item in place.
    setPendingAnchor(anchor);
    markDirty();
}

int VirtualItemView::itemDepth(const QModelIndex &index) const
{
    int depth = 0;
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
        ++depth;
    return depth;
}

bool VirtualItemView::handleItemKeyPress(QKeyEvent *event)
{
    Q_UNUSED(event);
    return false;
}

bool VirtualItemView::canMeasureItem(qsizetype item) const
{
    Q_UNUSED(item);
    return true;
}

bool VirtualItemView::usesItemWidgets() const
{
    return true;
}

void VirtualItemView::materializeItems(const VisibleRange &rows)
{
    Q_UNUSED(rows);
}

void VirtualItemView::materializeItemRanges(const QVector<VisibleRange> &ranges)
{
    materializeItems(ranges.isEmpty() ? VisibleRange() : ranges.first());
}

void VirtualItemView::augmentMaterializationRanges(QVector<VisibleRange> &ranges) const
{
    Q_UNUSED(ranges);
}

void VirtualItemView::rebindItemsInRange(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    rebindItemsInModelRange(topLeft.parent(), topLeft.row(), bottomRight.row());
}

QModelIndex VirtualItemView::indexForNavigation(qsizetype item, const QModelIndex &current) const
{
    Q_UNUSED(current);
    return viewIndex(item);
}

QItemSelection VirtualItemView::selectionRange(const QModelIndex &anchor,
                                                const QModelIndex &target) const
{
    if (anchor.isValid() && anchor.parent() == target.parent())
        return QItemSelection(anchor, target);
    QItemSelection selection;
    selection.select(target, target);
    return selection;
}

// ---------------------------------------------------------------------------
// Scrolling helpers
// ---------------------------------------------------------------------------

void VirtualItemView::setOverscan(int before, int after)
{
    const int clampedBefore = qMax(0, before);
    const int clampedAfter = qMax(0, after);
    if (m_overscanBefore == clampedBefore && m_overscanAfter == clampedAfter)
        return;
    m_overscanBefore = clampedBefore;
    m_overscanAfter = clampedAfter;
    markDirty();
}

void VirtualItemView::setWheelScrollMode(WheelScrollMode mode)
{
    m_wheelScrollMode = mode;
    syncScrollBars();
}

void VirtualItemView::setWheelScrollPixels(int pixels)
{
    m_wheelScrollPixels = qMax(1, pixels);
    m_wheelScrollMode = WheelScrollMode::Pixels;
    syncScrollBars();
}

void VirtualItemView::setWheelScrollItems(int items)
{
    m_wheelScrollItems = qMax(1, items);
    m_wheelScrollMode = WheelScrollMode::Items;
    syncScrollBars();
}

int VirtualItemView::viewportMainExtent() const
{
    if (m_layout && m_layout->orientation() == Qt::Horizontal)
        return viewport()->width();
    return viewport()->height();
}

qint64 VirtualItemView::contentExtent() const
{
    return m_layout ? m_layout->contentExtent() : 0;
}

qint64 VirtualItemView::maximumVerticalOffset() const
{
    return qMax<qint64>(0, contentExtent() - qint64(viewportMainExtent()));
}

VisibleRange VirtualItemView::coreVisibleRange() const
{
    VisibleRange range;
    if (!m_layout)
        return range;

    const qsizetype count = m_layout->itemCount();
    const qint64 viewExtent = viewportMainExtent();
    if (count <= 0 || viewExtent <= 0 || m_layout->contentExtent() <= 0)
        return range;
    const qsizetype topRows = qsizetype(frozenRows());
    const qsizetype bottomRows = qsizetype(frozenBottomRows());
    if (topRows >= count - bottomRows)
        return range;

    // The scrolling pane starts below the frozen band, so the visible content
    // window is shifted by its height (§31 row direction). Frozen rows are always
    // visible and are reported by visibleItemRanges(), not by this window.
    const qint64 top = m_scrollOffset + frozenTopExtent();
    const qint64 bottom = m_scrollOffset + viewExtent - frozenBottomExtent() - 1;
    if (bottom < top)
        return range; // the frozen bands cover the whole viewport

    qsizetype first = qMin(m_layout->indexAtOffset(top), count - 1);
    qsizetype last = qMin(m_layout->indexAtOffset(bottom), count - 1);
    const qsizetype lastScrollable = count - bottomRows - 1;
    first = qBound<qsizetype>(topRows, first, lastScrollable);
    last = qBound<qsizetype>(first, last, lastScrollable);
    range.first = first;
    range.last = last;
    return range;
}

VisibleRange VirtualItemView::visibleItemRange() const
{
    return coreVisibleRange();
}

QRect VirtualItemView::geometryForViewRow(qsizetype row) const
{
    if (!m_layout)
        return QRect();
    return m_layout->itemRect(row, itemPaneScrollOffset(itemPaneForRow(row)));
}

// ---------------------------------------------------------------------------
// Row panes (§31 row direction, docs/history/row-freezing.md)
// ---------------------------------------------------------------------------

int VirtualItemView::frozenRows() const
{
    if (!m_layout || m_frozenRows <= 0)
        return 0;
    const qsizetype count = m_layout->itemCount();
    const int viewHeight = viewport()->height();
    qsizetype rows = qBound<qsizetype>(qsizetype(0), m_frozenRows, count);
    if (viewHeight > 0)
        rows = qMin(rows, rowsFittingFromTop(viewHeight));
    return int(rows);
}

int VirtualItemView::frozenBottomRows() const
{
    if (!m_layout || m_frozenBottomRows <= 0)
        return 0;
    const qsizetype count = m_layout->itemCount();
    qsizetype rows = qBound<qsizetype>(qsizetype(0), m_frozenBottomRows,
                                       count - qsizetype(frozenRows()));
    const qint64 remaining = qint64(viewport()->height()) - frozenTopExtent();
    if (remaining <= 0)
        return 0;
    rows = qMin(rows, rowsFittingFromBottom(remaining));
    return int(rows);
}

void VirtualItemView::setFrozenRows(int count)
{
    const int clamped = qMax(0, count);
    if (m_frozenRows == clamped)
        return;
    m_frozenRows = clamped;
    markDirty();
}

void VirtualItemView::setFrozenBottomRows(int count)
{
    const int clamped = qMax(0, count);
    if (m_frozenBottomRows == clamped)
        return;
    m_frozenBottomRows = clamped;
    markDirty();
}

bool VirtualItemView::isRowFrozen(qsizetype row) const
{
    if (row < 0 || !m_layout)
        return false;
    const qsizetype count = m_layout->itemCount();
    const qsizetype top = frozenRows();
    if (row < top)
        return true;
    const qsizetype bottom = frozenBottomRows();
    return bottom > 0 && row >= count - bottom;
}

qint64 VirtualItemView::frozenTopExtent() const
{
    const qsizetype rows = frozenRows();
    if (rows <= 0 || !m_layout)
        return 0;
    return qMin<qint64>(m_layout->offsetOf(rows), m_layout->contentExtent());
}

qint64 VirtualItemView::frozenBottomExtent() const
{
    const qsizetype rows = frozenBottomRows();
    if (rows <= 0 || !m_layout)
        return 0;
    return qMax<qint64>(0, m_layout->contentExtent() - m_layout->offsetOf(m_layout->itemCount() - rows));
}

qsizetype VirtualItemView::rowsFittingFromTop(qint64 extent) const
{
    if (!m_layout || extent <= 0)
        return 0;
    const qsizetype count = m_layout->itemCount();
    const qint64 content = m_layout->contentExtent();
    if (extent >= content)
        return count;
    // The row containing the boundary may stick out of the pane, so only the rows
    // that end before it belong to the pane.
    return qBound<qsizetype>(qsizetype(0), m_layout->indexAtOffset(extent), count);
}

qsizetype VirtualItemView::rowsFittingFromBottom(qint64 extent) const
{
    if (!m_layout || extent <= 0)
        return 0;
    const qsizetype count = m_layout->itemCount();
    const qint64 content = m_layout->contentExtent();
    if (extent >= content)
        return count;
    const qsizetype row = m_layout->indexAtOffset(content - extent);
    if (row < 0)
        return 0;
    // Same rule from the other edge: the row the boundary falls into is not part of
    // the pane.
    return qBound<qsizetype>(qsizetype(0), count - row - 1, count);
}

ItemPane::Type VirtualItemView::itemPaneForRow(qsizetype row) const
{
    if (isRowFrozen(row)) {
        const qsizetype top = frozenRows();
        if (row < top)
            return ItemPane::Type::FrozenTop;
        return ItemPane::Type::FrozenBottom;
    }
    return ItemPane::Type::Scrollable;
}

qint64 VirtualItemView::itemPaneScrollOffset(ItemPane::Type type) const
{
    switch (type) {
    case ItemPane::Type::FrozenTop:
        return 0;
    case ItemPane::Type::Scrollable:
        return m_scrollOffset;
    case ItemPane::Type::FrozenBottom:
        // "Scrolled to the very bottom" pins the last rows to the bottom edge; the
        // value is signed on purpose, so it also works when the content is shorter
        // than the viewport.
        return contentExtent() - qint64(viewportMainExtent());
    }
    return m_scrollOffset;
}

QVector<ItemPane> VirtualItemView::itemPanes() const
{
    QVector<ItemPane> panes;
    if (!m_layout)
        return panes;
    const qsizetype count = m_layout->itemCount();
    const int viewHeight = viewport()->height();
    const int viewWidth = viewport()->width();
    if (count <= 0 || viewHeight <= 0)
        return panes;

    const qsizetype topRows = frozenRows();
    const qsizetype bottomRows = frozenBottomRows();
    const qint64 topExtent = qMin<qint64>(frozenTopExtent(), viewHeight);
    const qint64 bottomExtent = qMin<qint64>(frozenBottomExtent(), viewHeight - topExtent);

    if (topRows > 0 && topExtent > 0) {
        ItemPane top;
        top.type = ItemPane::Type::FrozenTop;
        top.viewportRect = QRect(0, 0, viewWidth, int(topExtent));
        top.firstRow = 0;
        top.lastRow = topRows - 1;
        panes.append(top);
    }

    ItemPane scrolling;
    scrolling.type = ItemPane::Type::Scrollable;
    scrolling.viewportRect = QRect(0, int(topExtent), viewWidth,
                                   int(viewHeight - topExtent - bottomExtent));
    scrolling.firstRow = topRows;
    scrolling.lastRow = count - bottomRows - 1;
    panes.append(scrolling);

    if (bottomRows > 0 && bottomExtent > 0) {
        ItemPane bottom;
        bottom.type = ItemPane::Type::FrozenBottom;
        bottom.viewportRect = QRect(0, int(viewHeight - bottomExtent), viewWidth, int(bottomExtent));
        bottom.firstRow = count - bottomRows;
        bottom.lastRow = count - 1;
        panes.append(bottom);
    }
    return panes;
}

QRect VirtualItemView::itemPaneRect(ItemPane::Type type) const
{
    for (const ItemPane &pane : itemPanes()) {
        if (pane.type == type)
            return pane.viewportRect;
    }
    return QRect();
}

ItemPane::Type VirtualItemView::itemPaneAtY(int y) const
{
    const QVector<ItemPane> panes = itemPanes();
    for (const ItemPane &pane : panes) {
        if (y >= pane.viewportRect.y()
            && y < pane.viewportRect.y() + pane.viewportRect.height()) {
            return pane.type;
        }
    }
    // Outside every pane (a degenerate viewport): fall back to the pane that owns
    // the edge the point is closest to.
    if (!panes.isEmpty() && y < panes.first().viewportRect.y())
        return panes.first().type;
    return panes.isEmpty() ? ItemPane::Type::Scrollable : panes.last().type;
}

QVector<VisibleRange> VirtualItemView::visibleItemRanges() const
{
    QVector<VisibleRange> ranges;
    if (frozenRows() > 0)
        ranges.append(VisibleRange{0, qsizetype(frozenRows()) - 1});
    const VisibleRange window = visibleItemRange();
    if (window.isValid())
        ranges.append(window);
    if (frozenBottomRows() > 0) {
        const qsizetype count = m_layout ? m_layout->itemCount() : 0;
        ranges.append(VisibleRange{count - qsizetype(frozenBottomRows()), count - 1});
    }
    return ranges;
}

void VirtualItemView::setItemPaneSeparatorStyle(const PaneSeparatorStyle &style)
{
    if (m_itemPaneSeparatorStyle == style)
        return;
    m_itemPaneSeparatorStyle = style;
    syncItemPanes();
}

QColor VirtualItemView::itemPaneSeparatorColor() const
{
    // No style specific separator to probe in the kernel: the palette's mid colour is the
    // honest default for a plain list. A table overrides this with the colour its style
    // paints section separators with, which is what its column boundary uses as well.
    return palette().color(QPalette::Mid);
}

QVector<QRect> VirtualItemView::itemPaneSeparatorRects() const
{
    QVector<QRect> rects;
    rects.reserve(m_itemPaneSeparatorLines.size());
    for (QWidget *line : m_itemPaneSeparatorLines) {
        if (line->isVisible())
            rects.append(line->geometry());
    }
    return rects;
}

void VirtualItemView::syncItemPanes()
{
    // The clip container only exists while something is frozen: an unused feature
    // changes nothing at all (the item widgets keep the viewport as parent).
    const QVector<ItemPane> panes = itemPanes();
    const QRect scrollRect = itemPaneRect(ItemPane::Type::Scrollable);
    if (panes.size() <= 1) {
        if (m_scrollPaneHost) {
            const QList<QWidget *> children =
                m_scrollPaneHost->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
            for (QWidget *child : children)
                reparentPreservingFocus(child, viewport());
            m_scrollPaneHost->hide();
            m_scrollPaneHost->setParent(nullptr);
            m_scrollPaneHost->deleteLater();
            m_scrollPaneHost = nullptr;
        }
    } else {
        if (!m_scrollPaneHost)
            m_scrollPaneHost = new ItemPaneClipHost(viewport());
        if (m_scrollPaneHost->geometry() != scrollRect)
            m_scrollPaneHost->setGeometry(scrollRect);
        m_scrollPaneHost->setVisible(!scrollRect.isEmpty());
    }

    // One line per pane boundary (top|scrolling and scrolling|bottom). The band
    // lies inside the pane above the boundary, so the lines of neighbouring panes
    // stay continuous.
    QVector<int> boundaries;
    for (int index = 0; index + 1 < panes.size(); ++index) {
        const ItemPane &before = panes.at(index);
        const ItemPane &after = panes.at(index + 1);
        if (before.viewportRect.height() <= 0 || after.viewportRect.height() <= 0)
            continue;
        boundaries.append(before.viewportRect.bottom() + 1);
    }
    while (m_itemPaneSeparatorLines.size() > boundaries.size()) {
        QWidget *line = m_itemPaneSeparatorLines.takeLast();
        line->hide();
        line->setParent(nullptr);
        line->deleteLater();
    }
    while (m_itemPaneSeparatorLines.size() < boundaries.size())
        // A child of the view, not of the viewport: the line also crosses whatever the
        // subclass keeps outside the viewport on the left (the row-number strip), exactly
        // like the column boundary lines cross the header strip.
        m_itemPaneSeparatorLines.append(new ItemPaneSeparatorLine(this));

    const QColor styleColor = itemPaneSeparatorColor();
    const QRect viewportRect = viewport()->geometry();
    const int leftExtension = qMax(0, itemPaneSeparatorLeftExtension());
    const int band = qMax(0, m_itemPaneSeparatorStyle.width);
    const int lineWidth = m_itemPaneSeparatorStyle.lineStyle == Qt::SolidLine ? band
                                                                             : qMax(1, band);
    for (int i = 0; i < boundaries.size(); ++i) {
        auto *line = static_cast<ItemPaneSeparatorLine *>(m_itemPaneSeparatorLines.at(i));
        line->setSeparator(m_itemPaneSeparatorStyle, styleColor);
        line->setGeometry(viewportRect.x() - leftExtension, viewportRect.y() + boundaries.at(i) - lineWidth,
                          viewportRect.width() + leftExtension, lineWidth);
        line->setVisible(m_itemPaneSeparatorStyle.isVisible() && lineWidth > 0);
    }
    raiseItemPaneSeparatorLines();
}

void VirtualItemView::raiseItemPaneSeparatorLines() const
{
    // The items are (re)created by every materialization pass, so the boundary lines
    // have to be lifted above them again. They are 1 px overlays that let input
    // through, so they can sit on top of everything.
    for (QWidget *line : m_itemPaneSeparatorLines)
        line->raise();
}

void VirtualItemView::syncRowSpacingWidgets(const QVector<VisibleRange> &ranges)
{
    auto *layout = dynamic_cast<ListLayout *>(m_layout);
    const quint64 modelSerial = m_modelChangeSerial;
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 lifecycleSerial = m_itemLifecycleSerial;
    const quint64 spacingSerial = m_rowSpacingStateSerial;
    const QPointer<QAbstractItemModel> activeModel(m_model);
    const bool hadModel = activeModel;
    WidgetAdapter *const activeAdapter = m_adapter;
    const RowSpacingFactory factory = m_rowSpacingFactory;
    const RowSpacingBinder binder = m_rowSpacingBinder;
    const auto requestIsCurrent = [this, layout, &activeModel, hadModel, activeAdapter,
                                   modelSerial, mappingSerial, lifecycleSerial, spacingSerial]() {
        return m_layout == layout && m_modelChangeSerial == modelSerial
            && viewMappingSerial() == mappingSerial && m_itemLifecycleSerial == lifecycleSerial
            && m_rowSpacingStateSerial == spacingSerial && m_model.data() == activeModel.data()
            && (!hadModel || activeModel) && m_adapter == activeAdapter;
    };
    const auto abortStalePass = [this, &requestIsCurrent]() {
        if (requestIsCurrent())
            return false;
        abortMaterializationPass();
        return true;
    };
    QHash<qsizetype, QRect> desired;
    if (layout && viewport()->width() > 0) {
        for (const VisibleRange &range : ranges) {
            for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
                const int spacing = layout->spacingAfter(row);
                if (spacing <= 0)
                    continue;
                const QRect itemRect = layout->itemRect(row, itemPaneScrollOffset(itemPaneForRow(row)));
                const QRect gap(0, itemRect.bottom() + 1, viewport()->width(), spacing);
                if (gap.intersects(itemPaneRect(itemPaneForRow(row)).intersected(viewport()->rect())))
                    desired.insert(row, gap);
            }
        }
    }
    if (abortStalePass())
        return;
    QList<qsizetype> obsolete;
    for (auto it = m_rowSpacingWidgets.cbegin(); it != m_rowSpacingWidgets.cend(); ++it) {
        if (!desired.contains(it.key()))
            obsolete.append(it.key());
    }
    for (qsizetype row : obsolete) {
        QWidget *widget = m_rowSpacingWidgets.take(row);
        QPointer<QWidget> guardedWidget(widget);
        if (!guardedWidget)
            continue;
        const int depth = widget->property("vivSpacingDepth").toInt();
        widget->hide();
        if (abortStalePass()) {
            if (guardedWidget)
                guardedWidget->deleteLater();
            return;
        }
        if (guardedWidget)
            m_rowSpacingPool[depth].append(guardedWidget.data());
    }
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        const qsizetype row = it.key();
        const QModelIndex index = viewIndex(row);
        if (abortStalePass())
            return;
        const int depth = itemDepth(index);
        if (abortStalePass())
            return;
        QWidget *widget = m_rowSpacingWidgets.value(row, nullptr);
        if (widget && widget->property("vivSpacingDepth").toInt() != depth) {
            m_rowSpacingWidgets.remove(row);
            QPointer<QWidget> guardedWidget(widget);
            const int oldDepth = widget->property("vivSpacingDepth").toInt();
            widget->hide();
            if (abortStalePass()) {
                if (guardedWidget)
                    guardedWidget->deleteLater();
                return;
            }
            if (guardedWidget)
                m_rowSpacingPool[oldDepth].append(guardedWidget.data());
            widget = nullptr;
        }
        if (!widget) {
            QVector<QWidget *> &pool = m_rowSpacingPool[depth];
            widget = pool.isEmpty() ? new RowSpacingHost(viewport()) : pool.takeLast();
            m_rowSpacingWidgets.insert(row, widget);
            QPointer<QWidget> guardedWidget(widget);
            widget->setProperty("vivSpacingDepth", depth);
            if (!guardedWidget) {
                if (m_rowSpacingWidgets.value(row) == widget)
                    m_rowSpacingWidgets.remove(row);
                abortMaterializationPass();
                return;
            }
            if (abortStalePass())
                return;
            auto *host = static_cast<RowSpacingHost *>(widget);
            if (factory && !host->content()) {
                QPointer<QWidget> content(factory(index, host));
                if (!guardedWidget) {
                    if (m_rowSpacingWidgets.value(row) == widget)
                        m_rowSpacingWidgets.remove(row);
                    if (content)
                        content->deleteLater();
                    abortMaterializationPass();
                    return;
                }
                if (abortStalePass()) {
                    if (content)
                        content->deleteLater();
                    return;
                }
                host->setContent(content.data());
                if (!guardedWidget) {
                    if (m_rowSpacingWidgets.value(row) == widget)
                        m_rowSpacingWidgets.remove(row);
                    abortMaterializationPass();
                    return;
                }
                if (abortStalePass())
                    return;
            }
        }
        QPointer<QWidget> guardedWidget(widget);
        const auto hostIsCurrent = [this, row, widget, &guardedWidget, &requestIsCurrent]() {
            if (guardedWidget && requestIsCurrent()
                && m_rowSpacingWidgets.value(row) == widget)
                return true;
            if (!guardedWidget && m_rowSpacingWidgets.value(row) == widget)
                m_rowSpacingWidgets.remove(row);
            abortMaterializationPass();
            return false;
        };
        auto *host = static_cast<RowSpacingHost *>(widget);
        configureRowSpacingWidget(host);
        if (!hostIsCurrent())
            return;
        host->setLineColor(itemPaneSeparatorColor());
        if (!hostIsCurrent())
            return;
        if (host->content() && binder)
            binder(host->content(), index);
        if (!hostIsCurrent())
            return;
        const bool scrolling = m_scrollPaneHost && !isRowFrozen(row);
        QWidget *parent = scrolling ? m_scrollPaneHost : viewport();
        const QPoint origin = scrolling ? itemPaneRect(ItemPane::Type::Scrollable).topLeft() : QPoint();
        if (widget->parentWidget() != parent)
            widget->setParent(parent);
        if (!hostIsCurrent())
            return;
        widget->setGeometry(it.value().translated(-origin));
        if (!hostIsCurrent())
            return;
        const QRegion mask = rowSpacingWidgetMask(row, it.value());
        if (!hostIsCurrent())
            return;
        if (mask == QRegion(widget->rect()))
            widget->clearMask();
        else
            widget->setMask(mask);
        if (!hostIsCurrent())
            return;
        widget->setVisible(!mask.isEmpty());
        if (!hostIsCurrent())
            return;
        widget->raise();
        if (!hostIsCurrent())
            return;
    }
    raiseItemPaneSeparatorLines();
}

void VirtualItemView::applyItemPaneGeometry(MaterializedItem &item, qsizetype row)
{
    QPointer<QWidget> widget(item.widget);
    if (!widget)
        return;
    const QPersistentModelIndex index = item.index;
    const QRect geometry = item.geometry;

    if (row < 0 || row >= viewItemCount()) {
        // A focused row may stay pinned after its tree branch collapses. Keep it
        // alive and focused without painting over the row now occupying its old slot.
        widget->move(-qMax(1, widget->width()), -qMax(1, widget->height()));
        return;
    }

    QWidget *parent = viewport();
    QPoint origin;
    bool frozen = false;
    if (m_scrollPaneHost) {
        frozen = row >= 0 && isRowFrozen(row);
        if (!frozen) {
            // Scrollable rows live in the clip container: Qt clips a widget to its
            // parent, so a row that scrolled behind the frozen band can never be
            // seen through it.
            parent = m_scrollPaneHost;
            origin = itemPaneRect(ItemPane::Type::Scrollable).topLeft();
        }
    }
    if (widget->parentWidget() != parent)
        reparentPreservingFocus(widget, parent);
    if (!widget || widgetForIndex(index) != widget.data())
        return;
    widget->setGeometry(geometry.translated(-origin));
    if (!widget || widgetForIndex(index) != widget.data())
        return;
    if (!widget->isVisible())
        widget->show();
    if (widget && widgetForIndex(index) == widget.data() && frozen)
        widget->raise();
}

qsizetype VirtualItemView::visibleItemCount() const
{
    const VisibleRange range = coreVisibleRange();
    if (range.isValid())
        return range.count();
    return (m_layout && m_layout->itemCount() > 0) ? 1 : 0;
}

qint64 VirtualItemView::wheelStepPixels() const
{
    if (m_wheelScrollMode == WheelScrollMode::Pixels)
        return qint64(qMax(1, m_wheelScrollPixels));

    int reference = 0;
    if (m_layout && m_layout->itemCount() > 0) {
        const qsizetype row = qMin(m_layout->indexAtOffset(m_scrollOffset), m_layout->itemCount() - 1);
        reference = m_layout->itemSize(row);
    }
    if (reference <= 0)
        reference = 32;
    return qint64(reference) * qint64(qMax(1, m_wheelScrollItems));
}

qint64 VirtualItemView::scrollBarSingleStepPixels() const
{
    if (m_wheelScrollMode == WheelScrollMode::Pixels)
        return qMax<qint64>(1, qint64(m_wheelScrollPixels) / 3);

    int reference = 0;
    if (m_layout && m_layout->itemCount() > 0) {
        const qsizetype row = qMin(m_layout->indexAtOffset(m_scrollOffset), m_layout->itemCount() - 1);
        reference = m_layout->itemSize(row);
    }
    return qMax<qint64>(1, reference > 0 ? reference : 32);
}

void VirtualItemView::setVerticalOffset(qint64 offset)
{
    const qint64 clamped = qBound<qint64>(qint64(0), offset, maximumVerticalOffset());
    if (clamped == m_scrollOffset)
        return;
    m_scrollOffset = clamped;
    const int value = m_scrollMapper.toScrollBarValue(m_scrollOffset);
    m_scrollMapper.setAnchor(m_scrollOffset, value);
    relayout();
}

void VirtualItemView::scrollByPixels(qint64 pixels)
{
    if (pixels == 0)
        return;
    setVerticalOffset(m_scrollOffset + pixels);
}

void VirtualItemView::scrollTo(const QModelIndex &index, ScrollHint hint)
{
    if (!m_layout)
        return;
    const qsizetype row = viewItemForIndex(index);
    if (row < 0)
        return;

    const qint64 viewExtent = viewportMainExtent();
    const qint64 start = m_layout->offsetOf(row);
    const qint64 size = m_layout->itemSize(row);
    qint64 offset = m_scrollOffset;

    // A frozen row is pinned: nothing to scroll for it (§31 row direction). The
    // scrolling rows are visible inside the scrolling pane, which starts below the
    // frozen band, so its height - not the viewport height - is what counts.
    if (isRowFrozen(row))
        return;
    const qint64 paneTop = frozenTopExtent();
    const qint64 paneExtent = qMax<qint64>(0, viewExtent - paneTop - frozenBottomExtent());

    switch (hint) {
    case EnsureVisible:
        if (start < offset + paneTop)
            offset = start - paneTop;
        else if (start + size > offset + paneTop + paneExtent)
            offset = start + size - paneTop - paneExtent;
        break;
    case PositionAtTop:
        offset = start - paneTop;
        break;
    case PositionAtBottom:
        offset = start + size - paneTop - paneExtent;
        break;
    case PositionAtCenter:
        offset = start + size / 2 - paneTop - paneExtent / 2;
        break;
    }

    m_scrollOffset = qBound<qint64>(qint64(0), offset, maximumVerticalOffset());
    relayout();
}

void VirtualItemView::setItemPinned(const QModelIndex &index, bool pinned)
{
    if (!index.isValid() || index.model() != m_model)
        return;
    const QModelIndex item = usesItemWidgets() ? index.siblingAtColumn(0) : index;
    if (!item.isValid())
        return;
    const QPersistentModelIndex persistent(item);
    if (pinned) {
        m_explicitPinned.insert(persistent);
        if (m_lifecycleLogEnabled)
            appendLifecycleLog(QStringLiteral("pin row=%1").arg(index.row()));
        checkPinLimit();
        // A pinned item has to be materialized even when it is outside the
        // overscan window, so schedule a pass.
        markDirty();
    } else {
        m_explicitPinned.remove(persistent);
        if (m_lifecycleLogEnabled)
            appendLifecycleLog(QStringLiteral("unpin row=%1").arg(index.row()));
        markDirty();
    }
}

bool VirtualItemView::isItemPinned(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != m_model)
        return false;
    const QModelIndex item = usesItemWidgets() ? index.siblingAtColumn(0) : index;
    return item.isValid() && (m_explicitPinned.contains(QPersistentModelIndex(item))
                             || item == m_dragSourcePin);
}

void VirtualItemView::pinWidget(QWidget *widget)
{
    if (!widget)
        return;
    const QModelIndex index = indexForWidget(widget);
    if (!index.isValid()) {
        qWarning("VirtualItemViews: pinWidget() called for a widget that is not a materialized item");
        return;
    }
    setItemPinned(index, true);
}

void VirtualItemView::unpinWidget(QWidget *widget)
{
    if (!widget)
        return;
    const QModelIndex index = indexForWidget(widget);
    if (!index.isValid()) {
        qWarning("VirtualItemViews: unpinWidget() called for a widget that is not a materialized item");
        return;
    }
    setItemPinned(index, false);
}

void VirtualItemView::setMaxPinnedItems(int max)
{
    m_maxPinnedItems = max;
    m_pinLimitWarned = false;
    checkPinLimit();
}

void VirtualItemView::checkPinLimit()
{
    if (m_maxPinnedItems <= 0)
        return;
    const qsizetype pinned = m_explicitPinned.size();
    if (pinned <= m_maxPinnedItems) {
        m_pinLimitWarned = false;
        return;
    }
    if (m_pinLimitWarned)
        return;
    m_pinLimitWarned = true;
    qWarning("VirtualItemViews: %lld items are pinned, above the configured limit of %d. "
             "Pinned items keep their QWidget alive and are not recycled.",
             qint64(pinned), m_maxPinnedItems);
}

VirtualViewStats VirtualItemView::stats() const
{
    VirtualViewStats result;
    result.logicalItems = viewItemCount();
    result.materializedItems = m_items.size();
    result.pooledWidgets = m_recycler ? m_recycler->pooledCount() : 0;
    result.pinnedWidgets = pinnedItemCount();
    result.createCount = quint64(m_recycler ? qMax<qsizetype>(0, m_recycler->createdCount()) : 0);
    result.bindCount = m_bindCount;
    result.recycleCount = m_recycleCount;
    return result;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

QModelIndex VirtualItemView::indexAt(const QPoint &viewportPos) const
{
    if (!m_layout)
        return QModelIndex();
    // Fold the point into the pane it belongs to (§31 row direction): a frozen pane
    // does not scroll, the bottom one is pinned to the bottom edge.
    const ItemPane::Type pane = itemPaneAtY(viewportPos.y());
    const qsizetype row = m_layout->itemAtPoint(viewportPos, itemPaneScrollOffset(pane));
    if (row < 0 || row >= viewItemCount())
        return QModelIndex();
    return viewIndex(row);
}

QRect VirtualItemView::visualRect(const QModelIndex &index) const
{
    if (!m_layout)
        return QRect();
    const qsizetype row = viewItemForIndex(index);
    if (row < 0)
        return QRect();
    return m_layout->itemRect(row, itemPaneScrollOffset(itemPaneForRow(row)));
}

QWidget *VirtualItemView::widgetForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    const auto it = m_itemLookup.constFind(QPersistentModelIndex(index));
    if (it == m_itemLookup.constEnd())
        return nullptr;
    return m_items.at(it.value()).widget;
}

QModelIndex VirtualItemView::indexForWidget(const QWidget *widget) const
{
    for (const MaterializedItem &item : m_items) {
        if (item.widget == widget)
            return QModelIndex(item.index);
    }
    return QModelIndex();
}

qsizetype VirtualItemView::pinnedItemCount() const
{
    qsizetype count = 0;
    for (const MaterializedItem &item : m_items) {
        if (item.pinned)
            ++count;
    }
    return count;
}

qsizetype VirtualItemView::pooledWidgetCount() const
{
    return m_recycler ? m_recycler->pooledCount() : 0;
}

qsizetype VirtualItemView::createdWidgetCount() const
{
    return m_recycler ? m_recycler->createdCount() : 0;
}

qsizetype VirtualItemView::destroyedWidgetCount() const
{
    return m_recycler ? m_recycler->destroyedCount() : 0;
}

void VirtualItemView::rebuildLookup()
{
    m_itemLookup.clear();
    m_itemLookup.reserve(m_items.size());
    for (qsizetype i = 0; i < m_items.size(); ++i)
        m_itemLookup.insert(m_items.at(i).index, i);
}

// ---------------------------------------------------------------------------
// Materialization
// ---------------------------------------------------------------------------

void VirtualItemView::markDirty()
{
    scheduleRelayout();
}

void VirtualItemView::scheduleRelayout()
{
    // One queued pass is enough for any number of invalidations: without this
    // guard 100 markDirty() calls post 100 queued events (all but the first are
    // no-ops, but they are 100 event-loop round trips).
    if (m_relayoutScheduled)
        return;
    m_relayoutScheduled = true;
    QMetaObject::invokeMethod(this, [this]() {
        if (!m_relayoutScheduled)
            return;
        m_relayoutScheduled = false;
        relayout();
    }, Qt::QueuedConnection);
}

void VirtualItemView::flushPendingRelayout()
{
    if (!m_relayoutScheduled)
        return;
    m_relayoutScheduled = false;
    relayout();
}

void VirtualItemView::abortMaterializationPass()
{
    m_materializationAborted = true;
    markDirty();
}

void VirtualItemView::relayout()
{
    if (m_inRelayout)
        return;

    // A view that materializes its own widgets (table cell mode) does not need
    // a WidgetAdapter: the kernel only computes ranges for it.
    if (!m_layout || (!m_adapter && usesItemWidgets())) {
        recycleAllItems();
        syncItemPanes();
        syncRowSpacingWidgets({});
        syncScrollBars();
        return;
    }

    // Safety net: a model may emit coarser signals than expected (proxy
    // models, custom models). The layout must always match the model.
    const qsizetype expectedCount = viewItemCount();
    if (m_layout->itemCount() != expectedCount) {
        m_layout->resetItems(expectedCount, estimateItemSize(expectedCount > 0 ? expectedCount - 1 : 0));
        applyRowSpacingOverrides();
    }

    if (m_anchorPending)
        applyPendingAnchor();

    m_inRelayout = true;
    m_materializationAborted = false;
    const quint64 modelSerial = m_modelChangeSerial;
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 lifecycleSerial = m_itemLifecycleSerial;
    const QPointer<VirtualItemView> self(this);
    const QPointer<QAbstractItemModel> activeModel(m_model);
    const bool hadModel = activeModel;
    WidgetAdapter *const activeAdapter = m_adapter;
    LayoutPolicy *const activeLayout = m_layout;
    const bool rowWidgets = usesItemWidgets();
    const auto requestIsCurrent = [self, &activeModel, hadModel, activeAdapter, activeLayout,
                                   modelSerial, mappingSerial, lifecycleSerial, rowWidgets]() {
        if (!self)
            return false;
        return self->m_modelChangeSerial == modelSerial && self->viewMappingSerial() == mappingSerial
            && self->m_itemLifecycleSerial == lifecycleSerial
            && self->m_model.data() == activeModel.data()
            && (!hadModel || activeModel)
            && self->m_adapter == activeAdapter && self->m_layout == activeLayout
            && self->usesItemWidgets() == rowWidgets;
    };
    const auto abortStalePass = [self, &requestIsCurrent]() {
        if (requestIsCurrent())
            return false;
        if (self) {
            self->m_inRelayout = false;
            self->abortMaterializationPass();
        }
        return true;
    };

    const qint64 viewExtent = viewportMainExtent();
    m_scrollOffset = qBound<qint64>(qint64(0), m_scrollOffset, maximumVerticalOffset());

    const qsizetype count = m_layout->itemCount();
    qsizetype firstRow = -1;
    qsizetype lastRow = -1;
    if (count > 0 && viewExtent > 0) {
        if (m_layout->contentExtent() <= 0) {
            // Nothing measurable yet (for example the first pass of a
            // "fit to first item" layout): materialize just enough rows to
            // measure, never the whole model.
            firstRow = 0;
            lastRow = qMin<qsizetype>(count - 1, qMax<qsizetype>(2, m_overscanAfter));
        } else {
            const VisibleRange visible = coreVisibleRange();
            const VisibleRange window = VisibleRange::expanded(visible.first, visible.last,
                                                              m_overscanBefore, m_overscanAfter,
                                                              count);
            if (window.isValid()) {
                firstRow = window.first;
                lastRow = window.last;
            }
        }
        // Frozen ranges are materialized separately. There may be no scrolling row
        // at all when the frozen top and bottom bands cover the entire model.
        const qsizetype top = qsizetype(frozenRows());
        const qsizetype lastScrollable = count - qsizetype(frozenBottomRows()) - 1;
        if (firstRow >= 0 && top <= lastScrollable) {
            firstRow = qBound<qsizetype>(top, firstRow, lastScrollable);
            lastRow = qBound<qsizetype>(firstRow, lastRow, lastScrollable);
        } else {
            firstRow = -1;
            lastRow = -1;
        }
    }

    // Materialization ranges: the scrolling window plus the frozen rows, which are
    // always on screen (§31 row direction) - exactly like frozen columns. The
    // ranges are disjoint: the window starts below the frozen top rows and ends
    // above the frozen bottom ones.
    QVector<VisibleRange> ranges;
    if (frozenRows() > 0)
        ranges.append(VisibleRange{0, qsizetype(frozenRows()) - 1});
    if (firstRow >= 0 && lastRow >= firstRow)
        ranges.append(VisibleRange{firstRow, lastRow});
    if (frozenBottomRows() > 0)
        ranges.append(VisibleRange{count - qsizetype(frozenBottomRows()), count - 1});

    // Explicit pins are materialized even when they are nowhere near the window: the
    // point of a pin is to let business code hold on to a widget while it does
    // something asynchronous, and that widget may be one that was never on screen
    // (P2-2). Every consumer expects disjoint ranges, so a pinned row is only added
    // when the ranges above do not already cover it.
    if (!m_explicitPinned.isEmpty() || m_dragSourcePin.isValid()) {
        QList<qsizetype> pinnedRows;
        pinnedRows.reserve(m_explicitPinned.size());
        for (const QPersistentModelIndex &persistent : m_explicitPinned) {
            const qsizetype row = viewItemForIndex(persistent);
            if (row >= 0 && row < count)
                pinnedRows.append(row);
        }
        const qsizetype dragRow = viewItemForIndex(m_dragSourcePin);
        if (dragRow >= 0 && dragRow < count)
            pinnedRows.append(dragRow);
        std::sort(pinnedRows.begin(), pinnedRows.end());
        qsizetype previous = -1;
        for (qsizetype row : pinnedRows) {
            if (row == previous)
                continue;
            previous = row;
            const auto covers = [row](const VisibleRange &range) {
                return row >= range.first && row <= range.last;
            };
            if (std::none_of(ranges.cbegin(), ranges.cend(), covers))
                ranges.append(VisibleRange{row, row});
        }
    }

    augmentMaterializationRanges(ranges);

    // ---- decide what to reuse, then recycle what is obsolete --------------
    if (!usesItemWidgets()) {
        // The view materializes its own widgets (table cell mode): it only needs
        // the ranges, not row widgets.
        materializeItemRanges(ranges);
        if (m_materializationAborted) {
            m_inRelayout = false;
            return;
        }
        syncItemPanes();
        syncRowSpacingWidgets(ranges);
        if (m_materializationAborted) {
            m_inRelayout = false;
            return;
        }
        m_inRelayout = false;
        syncScrollBars();
        afterMaterialize();
        refreshHoveredIndex();
        emit virtualizationUpdated();
        return;
    }

    // ---- decide what to reuse, then recycle what is obsolete --------------
    QList<QPair<qsizetype, QPersistentModelIndex>> desired;
    QSet<QPersistentModelIndex> desiredIndexes;
    for (const VisibleRange &range : ranges) {
        for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
            const QModelIndex index = viewIndex(row);
            if (abortStalePass())
                return;
            if (!index.isValid())
                continue;
            const QPersistentModelIndex persistent(index);
            if (desiredIndexes.contains(persistent))
                continue;
            desiredIndexes.insert(persistent);
            desired.append(qMakePair(row, persistent));
        }
    }

    // Recycling before creating lets the pool serve the incoming rows, so
    // steady state scrolling performs no allocation at all.
    for (qsizetype i = m_items.size(); i > 0;) {
        --i;
        MaterializedItem item = m_items.at(i);
        if (desiredIndexes.contains(item.index) || isPinnedItem(item))
            continue;
        m_items.removeAt(i);
        rebuildLookup();
        recycleItem(item);
        if (abortStalePass())
            return;
    }

    // ---- materialize ------------------------------------------------------
    QList<MaterializedItem> next;
    QList<qsizetype> nextRows;
    QSet<QPersistentModelIndex> nextIndexes;
    next.reserve(desired.size() + m_items.size());
    nextRows.reserve(desired.size() + m_items.size());

    for (const auto &target : desired) {
        const qsizetype row = target.first;
        const QPersistentModelIndex index = target.second;
        if (!index.isValid())
            continue;
        MaterializedItem item;
        const auto existing = m_itemLookup.constFind(index);
        if (existing != m_itemLookup.constEnd()) {
            item = m_items.at(existing.value());
        } else {
            item = createItem(index);
            if (abortStalePass())
                return;
            if (!item.widget)
                continue;
        }
        item.geometry = geometryForViewRow(row);
        if (abortStalePass())
            return;
        item.pinned = isPinnedItem(item);
        next.append(item);
        nextRows.append(row);
        nextIndexes.insert(index);
    }

    for (qsizetype i = m_items.size(); i > 0;) {
        --i;
        MaterializedItem item = m_items.at(i);
        if (nextIndexes.contains(item.index))
            continue;
        if (!isPinnedItem(item)) {
            m_items.removeAt(i);
            rebuildLookup();
            recycleItem(item);
            if (abortStalePass())
                return;
            continue;
        }
        const qsizetype row = viewItemForIndex(item.index);
        if (abortStalePass())
            return;
        item.geometry = row >= 0 && row < m_layout->itemCount()
            ? geometryForViewRow(row) : item.geometry;
        if (abortStalePass())
            return;
        item.pinned = true;
        next.append(item);
        nextRows.append(row >= 0 ? row : std::numeric_limits<qsizetype>::max());
    }

    // Deterministic order: top-to-bottom.
    QVector<qsizetype> order(int(next.size()));
    std::iota(order.begin(), order.end(), qsizetype(0));
    std::stable_sort(order.begin(), order.end(), [&nextRows](qsizetype lhs, qsizetype rhs) {
        return nextRows.at(lhs) < nextRows.at(rhs);
    });

    m_items.clear();
    m_items.reserve(next.size());
    for (qsizetype i : order)
        m_items.append(next.at(i));
    rebuildLookup();

    // ---- apply geometry ---------------------------------------------------
    // The row panes decide the parent (the scrolling rows are clipped into their
    // pane) and, for the frozen rows, lift them above the scrolling pane.
    syncItemPanes();
    if (abortStalePass())
        return;
    for (MaterializedItem &item : m_items) {
        applyItemPaneGeometry(item, viewItemForIndex(item.index));
        if (abortStalePass())
            return;
    }
    syncRowSpacingWidgets(ranges);
    if (abortStalePass())
        return;
    raiseItemPaneSeparatorLines();

    m_inRelayout = false;

    syncScrollBars();
    if (abortStalePass())
        return;
    afterMaterialize();
    if (abortStalePass())
        return;
    refreshHoveredIndex();
    if (abortStalePass())
        return;
    emit virtualizationUpdated();
}

MaterializedItem VirtualItemView::createItem(const QPersistentModelIndex &index)
{
    MaterializedItem item;
    item.index = index;
    if (!m_adapter || !index.isValid())
        return item;
    const quint64 modelSerial = m_modelChangeSerial;
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 lifecycleSerial = m_itemLifecycleSerial;
    const QPointer<VirtualItemView> self(this);
    const QPointer<QAbstractItemModel> activeModel(m_model);
    WidgetAdapter *const activeAdapter = m_adapter;
    LayoutPolicy *const activeLayout = m_layout;
    const auto requestIsCurrent = [self, &index, &activeModel, activeAdapter, activeLayout,
                                   modelSerial, mappingSerial, lifecycleSerial]() {
        return self && activeModel && self->m_model.data() == activeModel.data()
            && self->m_modelChangeSerial == modelSerial
            && self->viewMappingSerial() == mappingSerial
            && self->m_itemLifecycleSerial == lifecycleSerial && self->m_adapter == activeAdapter
            && self->m_layout == activeLayout && self->usesItemWidgets() && index.isValid();
    };
    const QModelIndex modelIndex(index);
    item.type = activeAdapter->widgetType(modelIndex);
    if (!requestIsCurrent() || QModelIndex(index) != modelIndex)
        return item;

    const qsizetype reuseBefore = m_recycler->reuseCount();
    QWidget *widget = m_recycler->acquire(item.type);
    if (!widget)
        return item;
    QPointer<QWidget> guardedWidget(widget);
    if (!requestIsCurrent() || QModelIndex(index) != modelIndex) {
        m_recycler->discard(guardedWidget.data());
        return item;
    }

    if (m_lifecycleLogEnabled) {
        appendLifecycleLog(QStringLiteral("%1 type=%2 row=%3")
                               .arg(m_recycler->reuseCount() == reuseBefore
                                        ? QStringLiteral("create")
                                        : QStringLiteral("reuse"))
                               .arg(item.type)
                               .arg(index.row()));
    }

    item.widget = widget;
    if (widget->parentWidget() != viewport())
        widget->setParent(viewport());
    if (!guardedWidget || !requestIsCurrent() || QModelIndex(index) != modelIndex) {
        m_recycler->discard(guardedWidget.data());
        item.widget = nullptr;
        return item;
    }
    // Never show a widget before it has been bound to its new index.
    widget->hide();
    if (!guardedWidget || !requestIsCurrent() || QModelIndex(index) != modelIndex) {
        m_recycler->discard(guardedWidget.data());
        item.widget = nullptr;
        return item;
    }
    m_items.append(item);
    rebuildLookup();
    m_bindingWidgets.insert(widget);
    const auto bindingIsCurrent = [self, &index, &modelIndex, &guardedWidget,
                                   &requestIsCurrent, widget]() {
        if (requestIsCurrent() && QModelIndex(index) == modelIndex && guardedWidget
            && self->widgetForIndex(index) == widget)
            return true;
        if (!self)
            return false;
        self->m_bindingWidgets.remove(widget);
        for (qsizetype i = 0; i < self->m_items.size(); ++i) {
            if (self->m_items.at(i).widget != widget)
                continue;
            self->m_items.removeAt(i);
            self->rebuildLookup();
            self->m_recycler->discard(guardedWidget.data());
            break;
        }
        return false;
    };
    activeAdapter->bindWidget(widget, modelIndex);
    if (!bindingIsCurrent()) {
        item.widget = nullptr;
        return item;
    }
    prepareHoverTracking(widget);
    if (!bindingIsCurrent()) {
        item.widget = nullptr;
        return item;
    }
    activeAdapter->visualStateChanged(widget, modelIndex);
    if (!bindingIsCurrent()) {
        item.widget = nullptr;
        return item;
    }
    m_bindingWidgets.remove(widget);
    ++m_bindCount;
    if (m_lifecycleLogEnabled)
        appendLifecycleLog(QStringLiteral("bind row=%1").arg(index.row()));
    return item;
}

void VirtualItemView::recycleItem(MaterializedItem &item)
{
    if (!item.widget)
        return;
    WidgetAdapter *const activeAdapter = m_adapter;
    const quint64 lifecycleSerial = m_itemLifecycleSerial;
    QPointer<QWidget> widget(item.widget);
    if (activeAdapter)
        activeAdapter->unbindWidget(item.widget, QModelIndex(item.index));
    clearVisualTransitionsForRow(QModelIndex(item.index));
    if (m_lifecycleLogEnabled)
        appendLifecycleLog(QStringLiteral("unbind row=%1").arg(item.index.row()));
    if (widget) {
        if (m_adapter == activeAdapter && activeAdapter && usesItemWidgets()
            && m_itemLifecycleSerial == lifecycleSerial)
            m_recycler->recycle(item.type, widget.data());
        else
            m_recycler->discard(widget.data());
    }
    ++m_recycleCount;
    if (m_lifecycleLogEnabled)
        appendLifecycleLog(QStringLiteral("recycle type=%1").arg(item.type));
    item.widget = nullptr;
}

void VirtualItemView::recycleAllItems()
{
    ++m_itemLifecycleSerial;
    QList<MaterializedItem> items;
    items.swap(m_items);
    m_itemLookup.clear();
    QVector<QPointer<QWidget>> widgets;
    widgets.reserve(items.size());
    for (const MaterializedItem &item : items)
        widgets.append(item.widget);
    WidgetAdapter *const activeAdapter = m_adapter;
    const quint64 lifecycleSerial = m_itemLifecycleSerial;
    for (qsizetype i = 0; i < items.size(); ++i) {
        MaterializedItem &item = items[i];
        m_bindingWidgets.remove(item.widget);
        item.widget = widgets.at(i).data();
        if (activeAdapter && m_adapter == activeAdapter && usesItemWidgets()
            && m_itemLifecycleSerial == lifecycleSerial)
            recycleItem(item);
        else if (item.widget) {
            m_recycler->discard(item.widget);
            item.widget = nullptr;
        }
    }
}

namespace {
bool ownsPopupWidget(const QWidget *widget, const QWidget *popup)
{
    if (!widget)
        return false;
    for (const QWidget *owner = popup; owner; owner = owner->parentWidget()) {
        if (owner == widget)
            return true;
        // QCompleter's parentless popup delegates focus to its editor.
        for (const QWidget *proxy = owner->focusProxy(); proxy; proxy = proxy->focusProxy()) {
            if (proxy == widget || widget->isAncestorOf(proxy))
                return true;
        }
    }
    return false;
}
} // namespace

void VirtualItemView::reparentPreservingFocus(QWidget *widget, QWidget *parent)
{
    if (!widget || widget->parentWidget() == parent)
        return;
    QPointer<QWidget> guardedWidget(widget);
    QPointer<QWidget> focus(QApplication::focusWidget());
    QPointer<QWidget> popup(QApplication::activePopupWidget());
    if (!ownsPopupWidget(widget, popup))
        popup.clear();
    const QRect popupGeometry = popup ? popup->geometry() : QRect();
    QVector<QPair<QPointer<QAbstractItemView>, QPersistentModelIndex>> popupCurrent;
    if (popup) {
        // Hiding an editor can reset the popup's uncommitted list highlight.
        auto views = popup->findChildren<QAbstractItemView *>();
        if (auto *view = qobject_cast<QAbstractItemView *>(popup.data()))
            views.prepend(view);
        for (QAbstractItemView *view : views)
            popupCurrent.append(qMakePair(QPointer<QAbstractItemView>(view),
                                         QPersistentModelIndex(view->currentIndex())));
    }
    if (focus && focus != widget && !widget->isAncestorOf(focus))
        focus.clear();
    widget->setParent(parent);
    if (guardedWidget && focus) {
        guardedWidget->show();
        if (guardedWidget && focus
            && (focus == guardedWidget || guardedWidget->isAncestorOf(focus)))
            focus->setFocus(Qt::OtherFocusReason);
    }
    if (guardedWidget && popup && !QApplication::activePopupWidget()) {
        if (ownsPopupWidget(guardedWidget, popup)) {
            popup->setGeometry(popupGeometry);
            popup->show();
            for (const auto &current : popupCurrent) {
                if (popup && current.first && current.second.isValid()
                    && current.first->model() == current.second.model())
                    current.first->setCurrentIndex(current.second);
            }
        }
    }
}

bool VirtualItemView::hasFocusWithin(const QWidget *widget) const
{
    if (!widget)
        return false;
    const QWidget *focus = QApplication::focusWidget();
    if (focus && (focus == widget || widget->isAncestorOf(focus)))
        return true;
    return ownsPopupWidget(widget, QApplication::activePopupWidget());
}

bool VirtualItemView::isPinnedItem(const MaterializedItem &item) const
{
    if (!item.widget)
        return false;
    if (isItemPinned(item.index))
        return true;
    // An open editor, an active IME composition or a popup must not be
    // recycled: in all three cases the widget (or one of its children) owns the
    // focus.
    return hasFocusWithin(item.widget);
}

void VirtualItemView::rebindItemsInModelRange(const QModelIndex &parent, int first, int last)
{
    if (!m_adapter)
        return;
    const quint64 modelSerial = m_modelChangeSerial;
    const quint64 mappingSerial = viewMappingSerial();
    const quint64 lifecycleSerial = m_itemLifecycleSerial;
    const QPointer<QAbstractItemModel> activeModel(m_model);
    WidgetAdapter *const activeAdapter = m_adapter;
    QList<QPersistentModelIndex> indexes;
    indexes.reserve(m_items.size());
    for (const MaterializedItem &item : m_items)
        indexes.append(item.index);
    for (const QPersistentModelIndex &persistent : indexes) {
        if (!activeModel || m_modelChangeSerial != modelSerial
            || viewMappingSerial() != mappingSerial || m_itemLifecycleSerial != lifecycleSerial
            || m_adapter != activeAdapter)
            return;
        const QModelIndex index = persistent;
        if (!index.isValid() || index.parent() != parent)
            continue;
        const int row = index.row();
        if (row < first || row > last)
            continue;
        QPointer<QWidget> widget(widgetForIndex(index));
        if (!widget || m_bindingWidgets.contains(widget.data()))
            continue;
        activeAdapter->bindWidget(widget.data(), index);
        if (!activeModel || m_modelChangeSerial != modelSerial
            || viewMappingSerial() != mappingSerial || m_itemLifecycleSerial != lifecycleSerial
            || m_adapter != activeAdapter)
            return;
        if (!widget || widgetForIndex(index) != widget.data())
            continue;
        activeAdapter->visualStateChanged(widget.data(), index);
        if (!activeModel || m_modelChangeSerial != modelSerial
            || viewMappingSerial() != mappingSerial || m_itemLifecycleSerial != lifecycleSerial
            || m_adapter != activeAdapter)
            return;
        ++m_bindCount;
        if (m_lifecycleLogEnabled)
            appendLifecycleLog(QStringLiteral("rebind row=%1").arg(row));
    }
}

void VirtualItemView::recycleItemsForColumnChange()
{
    // An explicit pin names a row, not a cell: remember which rows were pinned while the pin
    // still resolves (a removed column 0 would leave an invalid persistent index behind), then
    // hand every widget back through the adapter - unbindWidget() sees the index the widget was
    // really bound to, because the model has not changed yet.
    m_columnChangePinnedRows.clear();
    m_columnChangePinnedRows.reserve(m_explicitPinned.size());
    for (const QPersistentModelIndex &pinned : m_explicitPinned) {
        const qsizetype row = viewItemForIndex(pinned);
        if (row >= 0)
            m_columnChangePinnedRows.append(row);
    }
    m_columnChangePending = true;
    recycleAllItems();
}

void VirtualItemView::restoreItemsAfterColumnChange()
{
    if (!m_columnChangePending)
        return;
    m_columnChangePending = false;
    if (!m_columnChangePinnedRows.isEmpty()) {
        // The pins name rows, so they are re-keyed to the canonical (row, 0) cell instead of
        // pointing at a column that moved away or no longer exists.
        QSet<QPersistentModelIndex> repinned;
        for (qsizetype row : m_columnChangePinnedRows) {
            const QModelIndex canonical = viewIndex(row);
            if (canonical.isValid())
                repinned.insert(QPersistentModelIndex(canonical));
        }
        m_explicitPinned = repinned;
    }
    m_columnChangePinnedRows.clear();
    // The rows were released before the change: the next pass materializes them again, bound
    // to their canonical identity and with the WidgetType recomputed for it.
    markDirty();
}

void VirtualItemView::recycleItemsInModelRange(const QModelIndex &parent, int first, int last)
{
    if (m_items.isEmpty())
        return;

    QList<MaterializedItem> kept;
    QList<MaterializedItem> removedItems;
    kept.reserve(m_items.size());
    for (const MaterializedItem &item : m_items) {
        QModelIndex branch = item.index;
        while (branch.isValid() && branch.parent() != parent)
            branch = branch.parent();
        const bool removed = branch.isValid() && branch.row() >= first
            && branch.row() <= last;
        if (!removed) {
            kept.append(item);
            continue;
        }
        removedItems.append(item);
    }
    m_items.swap(kept);
    rebuildLookup();
    if (removedItems.isEmpty())
        return;
    ++m_itemLifecycleSerial;
    QVector<QPointer<QWidget>> widgets;
    widgets.reserve(removedItems.size());
    for (const MaterializedItem &item : removedItems)
        widgets.append(item.widget);
    WidgetAdapter *const activeAdapter = m_adapter;
    for (qsizetype i = 0; i < removedItems.size(); ++i) {
        MaterializedItem &item = removedItems[i];
        m_bindingWidgets.remove(item.widget);
        item.widget = widgets.at(i).data();
        m_explicitPinned.remove(item.index);
        if (activeAdapter && m_adapter == activeAdapter)
            recycleItem(item);
        else if (item.widget) {
            m_recycler->discard(item.widget);
            item.widget = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// Scroll anchor
// ---------------------------------------------------------------------------

ScrollAnchor VirtualItemView::captureAnchor() const
{
    ScrollAnchor anchor;
    if (!m_layout || m_layout->itemCount() == 0)
        return anchor;

    // The anchor is the first item of the *scrolling* pane, whose top edge sits
    // frozenTopExtent() pixels below the viewport top (§31 row direction).
    const qint64 contentOffset = m_scrollOffset + frozenTopExtent();
    const qsizetype row = m_layout->indexAtOffset(contentOffset);
    if (row < 0 || row >= m_layout->itemCount())
        return anchor;

    const QModelIndex index = viewIndex(row);
    if (!index.isValid())
        return anchor;

    anchor.index = QPersistentModelIndex(index);
    anchor.offsetInsideItem = int(contentOffset - m_layout->offsetOf(row));
    return anchor;
}

void VirtualItemView::setPendingAnchor(const ScrollAnchor &anchor)
{
    if (!anchor.isValid())
        return;
    m_pendingAnchor = anchor;
    m_anchorPending = true;
}

void VirtualItemView::cancelPendingAnchor()
{
    m_pendingAnchor = ScrollAnchor();
    m_anchorPending = false;
}

void VirtualItemView::applyPendingAnchor()
{
    m_anchorPending = false;
    if (!m_pendingAnchor.isValid() || !m_layout)
        return;

    const qsizetype row = viewItemForIndex(m_pendingAnchor.index);
    if (row < 0)
        return; // the anchored item disappeared: keep the numeric offset

    // Inverse of captureAnchor(): the scrolling pane starts below the frozen band.
    const qint64 contentOffset = m_layout->offsetOf(row) + m_pendingAnchor.offsetInsideItem;
    m_scrollOffset = qMax<qint64>(0, contentOffset - frozenTopExtent());
}

// ---------------------------------------------------------------------------
// Scrollbars
// ---------------------------------------------------------------------------

void VirtualItemView::syncScrollBars()
{
    QScrollBar *bar = verticalScrollBar();
    const int viewExtent = viewportMainExtent();
    const qint64 extent = m_layout ? m_layout->contentExtent() : 0;

    m_scrollMapper.setExtents(extent, viewExtent);

    const QSignalBlocker blocker(bar);
    bar->setRange(0, m_scrollMapper.scrollRange());
    const int value = m_scrollMapper.toScrollBarValue(m_scrollOffset);
    // Re-centre the compression window on the current viewport position so that
    // precision stays maximal where the user is looking.
    m_scrollMapper.setAnchor(m_scrollOffset, value);
    bar->setPageStep(qMax(1, viewExtent));
    bar->setSingleStep(int(qBound<qint64>(qint64(1), scrollBarSingleStepPixels(),
                                          qint64(std::numeric_limits<int>::max()))));
    bar->setValue(value);
}

void VirtualItemView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    if (m_inRelayout)
        return;

    const int value = verticalScrollBar()->value();
    m_scrollOffset = m_scrollMapper.toLogicalOffset(value);
    m_scrollMapper.setAnchor(m_scrollOffset, value);
    relayout();
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void VirtualItemView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().brush(QPalette::Base));
}

void VirtualItemView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    if (m_layout)
        m_layout->setCrossExtent(viewport()->width());
    if (m_recycler)
        m_recycler->trim();
    relayout();
}

void VirtualItemView::showEvent(QShowEvent *event)
{
    QAbstractScrollArea::showEvent(event);
    if (m_layout)
        m_layout->setCrossExtent(viewport()->width());
    relayout();
}

void VirtualItemView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    const QModelIndex index = indexAt(mousePosition(event));
    m_dragStartPos = mousePosition(event);
    m_pressedIndex = index.isValid() ? QPersistentModelIndex(index) : QPersistentModelIndex();
    updateSelectionForClick(index, event->modifiers());
    event->accept();
}

void VirtualItemView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
    stopDragAutoscroll();
    const QModelIndex index = indexAt(mousePosition(event));
    const bool sameIndex = index.isValid() && m_pressedIndex.isValid()
        && QModelIndex(m_pressedIndex) == index;
    m_pressedIndex = QPersistentModelIndex();
    event->accept();
    if (sameIndex)
        emit clicked(index);
}

void VirtualItemView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }
    const QModelIndex index = indexAt(mousePosition(event));
    event->accept();
    if (!index.isValid())
        return;
    const QPointer<VirtualItemView> self(this);
    const QPersistentModelIndex target(index);
    const quint64 modelSerial = modelChangeSerial();
    emit doubleClicked(target);
    if (!self || modelChangeSerial() != modelSerial || !target.isValid()
        || target.model() != model())
        return;
    emit activated(target);
}

// ---------------------------------------------------------------------------
// Drag & drop (§38)
// ---------------------------------------------------------------------------
//
// The kernel owns the interaction only: which item may start a drag (model
// flags), how the payload is built (model->mimeData()), where a drop lands
// (resolveDropTarget()) and how the gesture is drawn (drop indicator,
// autoscroll). Insertion, move and rejection remain in the model
// (canDropMimeData()/dropMimeData()), so no DnD logic reaches the Recycler.

void VirtualItemView::setDragEnabled(bool enabled)
{
    if (m_dragEnabled == enabled)
        return;
    m_dragEnabled = enabled;
    // Qt only delivers drag & drop events to a widget that accepts drops. The
    // widget under the cursor is the viewport, so it is the one that opts in;
    // QAbstractScrollArea then forwards the events to the view (like mouse
    // events), which is what the handlers below expect.
    viewport()->setAcceptDrops(enabled);
    if (!enabled)
        finishDrag();
}

bool VirtualItemView::canStartDrag(const QModelIndex &index) const
{
    if (!m_dragEnabled || !m_model || !index.isValid() || index.model() != m_model)
        return false;
    const QPointer<const VirtualItemView> self(this);
    const QPointer<QAbstractItemModel> sourceModel(m_model);
    const QPersistentModelIndex source(index);
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    const auto requestIsCurrent = [&]() {
        return self && sourceModel && self->modelChangeSerial() == modelSerial
            && self->viewMappingSerial() == mappingSerial && source.isValid()
            && source == index && self->m_dragEnabled;
    };
    const Qt::ItemFlags flags = sourceModel->flags(source);
    if (!requestIsCurrent() || !(flags & Qt::ItemIsDragEnabled))
        return false;
    const Qt::DropActions actions = dragDropActions();
    return requestIsCurrent()
        && (actions & (Qt::CopyAction | Qt::MoveAction | Qt::LinkAction)) != Qt::IgnoreAction;
}

Qt::DropActions VirtualItemView::dragDropActions() const
{
    if (m_dragDropActions != Qt::IgnoreAction)
        return m_dragDropActions;
    if (m_model) {
        const QPointer<const VirtualItemView> self(this);
        const QPointer<QAbstractItemModel> sourceModel(m_model);
        const quint64 modelSerial = modelChangeSerial();
        const quint64 mappingSerial = viewMappingSerial();
        const auto requestIsCurrent = [&]() {
            return self && sourceModel && self->modelChangeSerial() == modelSerial
                && self->viewMappingSerial() == mappingSerial
                && self->m_dragDropActions == Qt::IgnoreAction;
        };
        const Qt::DropActions supported = sourceModel->supportedDragActions();
        if (!requestIsCurrent())
            return Qt::IgnoreAction;
        if (supported != Qt::IgnoreAction)
            return supported;
        const Qt::DropActions drop = sourceModel->supportedDropActions();
        if (!requestIsCurrent())
            return Qt::IgnoreAction;
        if (drop != Qt::IgnoreAction)
            return drop;
    }
    return Qt::MoveAction | Qt::CopyAction;
}

void VirtualItemView::setDragDropActions(Qt::DropActions actions)
{
    m_dragDropActions = actions;
}

void VirtualItemView::setDropIndicatorShown(bool shown)
{
    m_dropIndicatorShown = shown;
    if (!shown)
        hideDropIndicator();
}

void VirtualItemView::setDefaultDropAction(Qt::DropAction action)
{
    m_defaultDropAction = action;
}

void VirtualItemView::setMoveRemovesSourceRows(bool enabled)
{
    m_moveRemovesSourceRows = enabled;
}

Qt::DropAction VirtualItemView::startDrag(const QModelIndex &index)
{
    if (!m_model || !m_dragSourceIndexes.isEmpty())
        return Qt::IgnoreAction;
    const QPointer<VirtualItemView> self(this);
    const QPointer<QAbstractItemModel> sourceModel = m_model;
    const quint64 modelSerial = m_modelChangeSerial;
    const quint64 mappingSerial = viewMappingSerial();

    QModelIndex dragIndex = index;
    if (!dragIndex.isValid()) {
        dragIndex = currentIndex();
        if (!dragIndex.isValid() && m_pressedIndex.isValid())
            dragIndex = QModelIndex(m_pressedIndex);
    }
    const QPersistentModelIndex sourceIndex(dragIndex);
    const auto requestIsCurrent = [&]() {
        return self && sourceModel && self->modelChangeSerial() == modelSerial
            && self->viewMappingSerial() == mappingSerial && sourceIndex.isValid()
            && sourceIndex == dragIndex && self->m_dragEnabled;
    };
    if (!canStartDrag(sourceIndex) || !requestIsCurrent())
        return Qt::IgnoreAction;

    // The selection is the payload when the dragged item is part of it - and it is handed over
    // column for column, like Qt does: a *row* selection (SelectionBehavior::SelectRows) carries
    // every column of those rows, so a drop can fill the new row 1:1, while a cell selection
    // carries just the dragged cell. Filtering the payload down to column 0 (the materialized row
    // identity) used to lose every other column of a row drag.
    QModelIndexList indexes = dragSourceIndexes(dragIndex);
    if (!requestIsCurrent() || indexes.isEmpty())
        return Qt::IgnoreAction;
    const Qt::DropActions actions = dragDropActions();
    if (!requestIsCurrent() || actions == Qt::IgnoreAction)
        return Qt::IgnoreAction;

    QMimeData *mime = sourceModel->mimeData(indexes);
    if (!mime)
        return Qt::IgnoreAction;
    if (!requestIsCurrent()) {
        delete mime;
        return Qt::IgnoreAction;
    }

    // Keep the actual materialized source alive without taking ownership of a user pin.
    const QPointer<QWidget> sourceWidget(dragSourceWidget(dragIndex));
    if (!requestIsCurrent()) {
        delete mime;
        return Qt::IgnoreAction;
    }
    m_dragSourceWidget = sourceWidget;
    m_dragSourcePin = m_dragSourceWidget
        ? QPersistentModelIndex(indexForWidget(m_dragSourceWidget)) : QPersistentModelIndex();
    // The sources are remembered so the drag can refuse to drop onto itself.
    m_dragSourceIndexes.clear();
    for (const QModelIndex &source : indexes)
        m_dragSourceIndexes.append(QPersistentModelIndex(source));

    QDrag drag(this);
    drag.setMimeData(mime);
    if (m_dragSourceWidget) {
        // The preview is what the drop moves: a row widget (list / tree / a table row drag), or
        // just the dragged cell when the table works per cell - dragPixmapRect() names the part of
        // the widget that belongs to the dragged index.
        const QRect part = dragPixmapRect(dragIndex);
        const QPixmap pixmap = part.isEmpty() ? m_dragSourceWidget->grab()
                                              : m_dragSourceWidget->grab(part);
        drag.setPixmap(pixmap);
        drag.setHotSpot(pixmap.rect().center());
    }
    const Qt::DropAction action = drag.exec(actions, m_defaultDropAction);

    // A drag that ended in a *move* moved the item somewhere else: if the application asked this
    // view to own that cleanup (setMoveRemovesSourceRows()), the dragged rows go now - exactly
    // when QAbstractItemView does it (after QDrag::exec(), before the drag state is released).
    const QList<QPersistentModelIndex> sources = m_dragSourceIndexes;
    finishDrag();
    if (action == Qt::MoveAction && m_moveRemovesSourceRows
        && sourceModel && m_modelChangeSerial == modelSerial)
        removeDraggedSourceRows(sources);
    return action;
}

QModelIndexList VirtualItemView::dragSourceIndexes(const QModelIndex &dragIndex) const
{
    QModelIndexList indexes;
    if (!m_model || !dragIndex.isValid())
        return indexes;
    const QPointer<const VirtualItemView> self(this);
    const QPointer<QAbstractItemModel> sourceModel(m_model);
    const QPersistentModelIndex source(dragIndex);
    const QPointer<QItemSelectionModel> selection(m_selectionModel);
    const quint64 modelSerial = modelChangeSerial();
    const quint64 mappingSerial = viewMappingSerial();
    const auto requestIsCurrent = [&]() {
        return self && sourceModel && self->modelChangeSerial() == modelSerial
            && self->viewMappingSerial() == mappingSerial && source.isValid()
            && source == dragIndex && self->m_selectionModel == selection;
    };
    const bool sourceSelected = selection && selection->isSelected(source);
    if (!requestIsCurrent())
        return {};
    if (sourceSelected) {
        const QModelIndexList selected = selection->selectedIndexes();
        if (!requestIsCurrent())
            return {};
        for (const QModelIndex &candidate : selected) {
            const Qt::ItemFlags flags = sourceModel->flags(candidate);
            if (!requestIsCurrent())
                return {};
            if (flags & Qt::ItemIsDragEnabled)
                indexes.append(candidate);
        }
    }
    if (indexes.isEmpty())
        indexes.append(dragIndex);
    return indexes;
}

QWidget *VirtualItemView::dragSourceWidget(const QModelIndex &index) const
{
    const qsizetype item = viewItemForIndex(index);
    return item >= 0 ? widgetForIndex(viewIndex(item, 0)) : nullptr;
}

QRect VirtualItemView::dragPixmapRect(const QModelIndex &index) const
{
    Q_UNUSED(index);
    return QRect();   // the whole item widget is the preview
}

QModelIndex VirtualItemView::dragNodeIndex(const QModelIndex &index) const
{
    return index;
}

void VirtualItemView::removeDraggedSourceRows(const QList<QPersistentModelIndex> &sources)
{
    if (!m_model || sources.isEmpty())
        return;
    const QPointer<QAbstractItemModel> sourceModel = m_model;
    const quint64 modelSerial = m_modelChangeSerial;

    // Group the dragged rows by parent: a tree drag can span several parents, and a dragged
    // parent already covers the dragged children below it (they are removed with it).
    QHash<QPersistentModelIndex, QList<qsizetype>> rowsPerParent;
    for (const QPersistentModelIndex &source : sources) {
        if (!source.isValid() || source.model() != sourceModel)
            continue;   // the model removed it already (it performed the move itself)
        rowsPerParent[QPersistentModelIndex(source.parent())].append(source.row());
    }
    if (rowsPerParent.isEmpty())
        return;

    const auto depthOf = [](const QModelIndex &index) {
        int depth = 0;
        for (QModelIndex ancestor = index; ancestor.isValid(); ancestor = ancestor.parent())
            ++depth;
        return depth;
    };
    QList<QPersistentModelIndex> parents = rowsPerParent.keys();
    // Deepest parents first: removing a child's rows first keeps the parent's own row valid,
    // and a parent that is dragged as well takes its (now empty) children with it.
    std::sort(parents.begin(), parents.end(),
              [&depthOf](const QPersistentModelIndex &lhs, const QPersistentModelIndex &rhs) {
                  return depthOf(lhs) > depthOf(rhs);
              });

    for (const QPersistentModelIndex &parent : parents) {
        if (!sourceModel || m_modelChangeSerial != modelSerial
            || (parent.isValid() && parent.model() != sourceModel))
            return;
        const bool hasParent = parent.isValid();
        QList<qsizetype> rows = rowsPerParent.value(parent);
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        // Walk the runs from the last one upwards so the earlier rows keep their positions.
        qsizetype runEnd = rows.size() - 1;
        while (runEnd >= 0) {
            if (!sourceModel || m_modelChangeSerial != modelSerial
                || (hasParent && !parent.isValid()))
                return;
            qsizetype runStart = runEnd;
            while (runStart > 0 && rows.at(runStart - 1) + 1 == rows.at(runStart))
                --runStart;
            const qsizetype count = runEnd - runStart + 1;
            sourceModel->removeRows(int(rows.at(runStart)), int(count), QModelIndex(parent));
            runEnd = runStart - 1;
        }
    }
}

void VirtualItemView::finishDrag()
{
    const bool hadSourcePin = m_dragSourcePin.isValid();
    m_dragSourceWidget = nullptr;
    m_dragSourcePin = QPersistentModelIndex();
    if (hadSourcePin)
        markDirty();
    stopDragAutoscroll();
    hideDropIndicator();
    m_dragSourceIndexes.clear();
    m_dragHoverValid = false;
}

bool VirtualItemView::isDropOnItself(const DropTarget &target, Qt::DropAction action) const
{
    // QAbstractItemView only guards internal *moves*: copying an item next to
    // itself is a legitimate gesture.
    if (!target.isValid() || action != Qt::MoveAction || m_dragSourceIndexes.isEmpty())
        return false;
    for (const QPersistentModelIndex &persistent : m_dragSourceIndexes) {
        if (!persistent.isValid())
            continue;
        const QModelIndex source = dragNodeIndex(QModelIndex(persistent));
        if (!source.isValid())
            continue;
        // Into the dragged item, or into one of its descendants.
        for (QModelIndex ancestor = target.parent; ancestor.isValid(); ancestor = ancestor.parent()) {
            if (ancestor == source)
                return true;
        }
        // Between the dragged item and itself: the item would not move.
        if (target.parent == source.parent()
            && (target.row == source.row() || target.row == source.row() + 1))
            return true;
    }
    return false;
}

void VirtualItemView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragEnabled || !(event->buttons() & Qt::LeftButton) || !m_pressedIndex.isValid()) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    const QPoint pos = mousePosition(event);
    if ((pos - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) {
        event->accept();
        return;
    }

    // The pressed index (not the position) carries the gesture: it survives
    // model mutations between press and move.
    const QModelIndex index(m_pressedIndex);
    m_pressedIndex = QPersistentModelIndex();
    event->accept();
    if (canStartDrag(index))
        startDrag(index);
}

void VirtualItemView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!m_dragEnabled || !m_model || !event->mimeData()) {
        event->ignore();
        return;
    }
    const Qt::DropActions supported = m_model->supportedDropActions();
    if (!(supported & event->proposedAction()) && !(supported & event->possibleActions())) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void VirtualItemView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!m_model || !event->mimeData()) {
        event->ignore();
        return;
    }
    const QPoint pos = dropPosition(event);
    m_dragHoverPos = pos;
    m_dragHoverValid = true;
    const DropTarget target = resolveDropTarget(pos);
    const QPointer<QAbstractItemModel> dropModel = m_model;
    const quint64 modelSerial = m_modelChangeSerial;
    const QPersistentModelIndex parent(target.parent);
    const Qt::DropAction action = event->dropAction() != Qt::IgnoreAction
        ? event->dropAction()
        : event->proposedAction();
    const bool canDrop = target.isValid() && !isDropOnItself(target, action)
        && dropModel->canDropMimeData(event->mimeData(), action, target.row, target.column,
                                      target.parent);
    if (!canDrop || !dropModel || m_modelChangeSerial != modelSerial
        || (target.parent.isValid() && !parent.isValid())) {
        hideDropIndicator();
        stopDragAutoscroll();
        event->ignore();
        return;
    }
    showDropIndicator(target);
    updateDragAutoscroll(pos);
    event->accept();
}

void VirtualItemView::dragLeaveEvent(QDragLeaveEvent *event)
{
    hideDropIndicator();
    stopDragAutoscroll();
    m_dragHoverValid = false;
    event->accept();
}

void VirtualItemView::dropEvent(QDropEvent *event)
{
    stopDragAutoscroll();
    hideDropIndicator();
    m_dragHoverValid = false;
    if (!m_model || !event->mimeData()) {
        event->ignore();
        return;
    }
    const DropTarget target = resolveDropTarget(dropPosition(event));
    const QPointer<QAbstractItemModel> dropModel = m_model;
    const quint64 modelSerial = m_modelChangeSerial;
    const QPersistentModelIndex parent(target.parent);
    const Qt::DropAction action = event->dropAction() != Qt::IgnoreAction
        ? event->dropAction()
        : event->proposedAction();
    if (!target.isValid() || isDropOnItself(target, action)
        || !dropModel->canDropMimeData(event->mimeData(), action, target.row, target.column,
                                       target.parent)
        || !dropModel || m_modelChangeSerial != modelSerial
        || (target.parent.isValid() && !parent.isValid())) {
        event->ignore();
        return;
    }
    // The model owns the semantics: it inserts, moves or rejects (§38).
    const bool accepted = dropModel->dropMimeData(event->mimeData(), action, target.row,
                                                   target.column, QModelIndex(parent));
    if (!dropModel || m_modelChangeSerial != modelSerial) {
        event->ignore();
        return;
    }
    if (accepted) {
        event->acceptProposedAction();
        emit itemDropped(QModelIndex(parent), target.row, target.column, action);
        markDirty();
    } else {
        event->ignore();
    }
}

VirtualItemView::DropTarget VirtualItemView::resolveDropTarget(const QPoint &viewportPos) const
{
    DropTarget target;
    if (!m_layout || m_layout->itemCount() <= 0)
        return target;
    // Same pane mapping as indexAt(): a frozen top row does not scroll and the
    // bottom band is pinned to the bottom edge, so the content offset under the
    // cursor is not "scrollOffset + y" once rows are frozen (§31 row direction).
    const ItemPane::Type pane = itemPaneAtY(viewportPos.y());
    const qint64 offset = itemPaneScrollOffset(pane) + qMax(0, viewportPos.y());
    if (offset < 0 || offset >= m_layout->contentExtent())
        return target;
    const qsizetype row = m_layout->indexAtOffset(offset);
    if (row < 0 || row >= m_layout->itemCount())
        return target;
    const qint64 rowOffset = m_layout->offsetOf(row);
    const bool after = offset - rowOffset > m_layout->itemSize(row) / 2;
    const QModelIndex index = viewIndex(row);
    if (!index.isValid())
        return target;
    target.parent = index.parent();
    target.row = index.row() + (after ? 1 : 0);
    target.column = -1;
    return target;
}

QRect VirtualItemView::resolveDropIndicatorRect(const DropTarget &target) const
{
    if (!target.isValid() || !m_layout || m_layout->itemCount() <= 0)
        return QRect();
    // The line sits at the boundary the insertion would use: below the item that
    // precedes the insertion point, or above the row that follows it.
    const QModelIndex before = target.row > 0
        ? m_model->index(target.row - 1, 0, target.parent)
        : QModelIndex();
    qint64 offset = -1;
    // The line belongs to the band of the row it refers to: a frozen row keeps the
    // frozen band's mapping instead of the scrolling offset.
    ItemPane::Type pane = ItemPane::Type::Scrollable;
    if (before.isValid()) {
        const qsizetype row = viewItemForIndex(before);
        if (row >= 0) {
            offset = m_layout->offsetOf(row) + m_layout->itemSize(row);
            pane = itemPaneForRow(row);
        }
    }
    if (offset < 0) {
        const QModelIndex at = m_model->index(target.row, 0, target.parent);
        const qsizetype row = at.isValid() ? viewItemForIndex(at) : -1;
        if (row < 0)
            return QRect();
        offset = m_layout->offsetOf(row);
        pane = itemPaneForRow(row);
    }
    const int y = int(offset - itemPaneScrollOffset(pane));
    return QRect(0, y - 1, viewport()->width(), 2);
}

VirtualItemView::DropTarget VirtualItemView::dropTargetAt(const QPoint &viewportPos) const
{
    return resolveDropTarget(viewportPos);
}

QRect VirtualItemView::dropIndicatorRect(const DropTarget &target) const
{
    return resolveDropIndicatorRect(target);
}

VirtualItemView::DropIndicatorStyle
VirtualItemView::dropIndicatorStyle(const DropTarget &target) const
{
    return target.ontoItem ? DropIndicatorStyle::Frame : DropIndicatorStyle::Line;
}

void VirtualItemView::showDropIndicator(const DropTarget &target)
{
    if (!m_dropIndicatorShown)
        return;
    const QRect rect = resolveDropIndicatorRect(target);
    if (rect.isEmpty()) {
        hideDropIndicator();
        return;
    }
    const bool frame = dropIndicatorStyle(target) == DropIndicatorStyle::Frame;
    if (!m_dropIndicator) {
        m_dropIndicator = new DropIndicatorWidget(viewport());
        viewport()->update();
    }
    static_cast<DropIndicatorWidget *>(m_dropIndicator)->setFrame(frame);
    m_dropIndicator->setGeometry(rect);
    m_dropIndicator->show();
    m_dropIndicator->raise();
}

void VirtualItemView::hideDropIndicator()
{
    if (m_dropIndicator)
        m_dropIndicator->hide();
}

void VirtualItemView::updateDragAutoscroll(const QPoint &viewportPos)
{
    const int margin = qMax(8, viewport()->height() / 12);
    int delta = 0;
    if (viewportPos.y() < margin)
        delta = -qMax(1, margin - viewportPos.y());
    else if (viewportPos.y() > viewport()->height() - margin)
        delta = qMax(1, viewportPos.y() - (viewport()->height() - margin));
    if (delta == 0) {
        stopDragAutoscroll();
        return;
    }
    m_dragAutoscrollDelta = delta;
    if (!m_dragAutoscrollTimer) {
        m_dragAutoscrollTimer = new QTimer(this);
        m_dragAutoscrollTimer->setInterval(40);
        connect(m_dragAutoscrollTimer, &QTimer::timeout, this, [this]() {
            const qint64 before = m_scrollOffset;
            scrollByPixels(qBound(-40, m_dragAutoscrollDelta, 40));
            if (m_scrollOffset == before || !m_dragHoverValid)
                return;
            // The rows below the cursor moved with the content: re-resolve the
            // target so the indicator stays glued to the insertion point (and
            // gets lifted above the widgets materialized by the scroll).
            const DropTarget target = resolveDropTarget(m_dragHoverPos);
            if (target.isValid())
                showDropIndicator(target);
            else
                hideDropIndicator();
        });
    }
    if (!m_dragAutoscrollTimer->isActive())
        m_dragAutoscrollTimer->start();
}

void VirtualItemView::stopDragAutoscroll()
{
    if (m_dragAutoscrollTimer)
        m_dragAutoscrollTimer->stop();
    m_dragAutoscrollDelta = 0;
}

void VirtualItemView::wheelEvent(QWheelEvent *event)
{
    // High resolution pixel deltas take precedence even in Items mode.
    const QPoint pixel = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    if (pixel.isNull() && angle.y() != 0 && angle.y() % 120 == 0
        && m_wheelScrollMode == WheelScrollMode::Items && m_layout
        && m_layout->itemCount() > 0) {
        const qsizetype first = frozenRows();
        const qsizetype last = m_layout->itemCount() - frozenBottomRows() - 1;
        if (first <= last) {
            // Frozen rows are outside the wheel's window. Actual row starts
            // include variable heights and spacing, unlike a reference-height step.
            const qsizetype row = qBound(first,
                m_layout->indexAtOffset(m_scrollOffset + frozenTopExtent()), last);
            const qint64 steps = -qint64(angle.y()) / 120 * qMax(1, m_wheelScrollItems);
            if (steps < qint64(first - row))
                setVerticalOffset(0);
            else if (steps > qint64(last - row))
                setVerticalOffset(maximumVerticalOffset());
            else
                scrollByPixels(m_layout->offsetOf(row + qsizetype(steps)) - m_layout->offsetOf(row));
        }
        event->accept();
        return;
    }
    qint64 delta = 0;
    if (!pixel.isNull()) {
        delta = qint64(pixel.y());
    } else if (!angle.isNull()) {
        delta = qint64(angle.y()) * wheelStepPixels() / 120;
    }
    if (delta == 0) {
        event->ignore();
        return;
    }

    // Wheel up (positive delta) moves the content down.
    scrollByPixels(-delta);
    event->accept();
}

void VirtualItemView::keyPressEvent(QKeyEvent *event)
{
    if (handleItemKeyPress(event)) {
        event->accept();
        return;
    }

    const qsizetype count = viewItemCount();
    const QModelIndex current = currentIndex();
    const qsizetype currentRow = current.isValid() ? viewItemForIndex(current) : -1;
    const qsizetype page = qMax<qsizetype>(1, visibleItemCount());

    switch (event->key()) {
    case Qt::Key_Up:
        moveCurrentTo(currentRow < 0 ? count - 1 : currentRow - 1, event->modifiers());
        event->accept();
        return;
    case Qt::Key_Down:
        moveCurrentTo(currentRow < 0 ? 0 : currentRow + 1, event->modifiers());
        event->accept();
        return;
    case Qt::Key_PageUp:
        moveCurrentTo(currentRow < 0 ? 0 : currentRow - page, event->modifiers());
        event->accept();
        return;
    case Qt::Key_PageDown:
        moveCurrentTo(currentRow < 0 ? 0 : currentRow + page, event->modifiers());
        event->accept();
        return;
    case Qt::Key_Home:
        moveCurrentTo(0, event->modifiers());
        event->accept();
        return;
    case Qt::Key_End:
        moveCurrentTo(count - 1, event->modifiers());
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (current.isValid())
            emit activated(current);
        event->accept();
        return;
    case Qt::Key_Space:
        // Space toggles the current item (like QAbstractItemView) - but never in
        // NoSelection mode, where nothing may be selected at all.
        if (m_selectionModel && current.isValid()) {
            const bool wasSelected = m_selectionModel->isSelected(current);
            const QItemSelectionModel::SelectionFlags command = selectionFlagsFor(
                wasSelected ? QItemSelectionModel::Deselect : QItemSelectionModel::Select);
            if (command != QItemSelectionModel::NoUpdate) {
                m_selectionModel->select(selectionRange(current, current), command);
                pinCurrentIndex(current);
            }
        }
        event->accept();
        return;
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void VirtualItemView::moveCurrentToItem(qsizetype item, Qt::KeyboardModifiers modifiers)
{
    moveCurrentTo(item, modifiers);
}

void VirtualItemView::moveCurrentTo(qsizetype row, Qt::KeyboardModifiers modifiers)
{
    const qsizetype count = viewItemCount();
    if (count <= 0 || !m_layout)
        return;

    row = qBound<qsizetype>(qsizetype(0), row, count - 1);
    const QModelIndex index = indexForNavigation(row, currentIndex());
    if (!index.isValid())
        return;

    if (m_selectionModel) {
        switch (m_selectionMode) {
        case SelectionMode::NoSelection:
            m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current);
            break;
        case SelectionMode::SingleSelection:
            selectCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
            m_selectionAnchor = QPersistentModelIndex(index);
            break;
        case SelectionMode::MultiSelection:
        case SelectionMode::ExtendedSelection:
            if (modifiers & Qt::ShiftModifier) {
                extendSelectionTo(index);
            } else if (m_selectionMode == SelectionMode::ExtendedSelection
                       && (modifiers & Qt::ControlModifier)) {
                // Move the current index and leave the selection untouched.
                m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current | rowFlags());
            } else {
                selectCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
                pinCurrentIndex(index);
                m_selectionAnchor = QPersistentModelIndex(index);
            }
            break;
        }
    }
    scrollTo(index, EnsureVisible);
}

void VirtualItemView::updateSelectionForClick(const QModelIndex &index, Qt::KeyboardModifiers modifiers)
{
    if (!m_selectionModel || !index.isValid())
        return;

    switch (m_selectionMode) {
    case SelectionMode::NoSelection:
        // The current index still moves; nothing is ever selected.
        m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current);
        return;
    case SelectionMode::SingleSelection:
        selectCurrentIndex(index, selectionFlagsFor(QItemSelectionModel::ClearAndSelect));
        pinCurrentIndex(index);
        m_selectionAnchor = QPersistentModelIndex(index);
        return;
    case SelectionMode::MultiSelection:
    case SelectionMode::ExtendedSelection:
        break;
    }

    if (modifiers & Qt::ShiftModifier) {
        extendSelectionTo(index);
        return;
    }
    // MultiSelection toggles on every click; ExtendedSelection only with Ctrl.
    if (m_selectionMode == SelectionMode::MultiSelection || (modifiers & Qt::ControlModifier)) {
        toggleClickedIndex(index);
        return;
    }
    selectCurrentIndex(index, selectionFlagsFor(QItemSelectionModel::ClearAndSelect));
    pinCurrentIndex(index);
    m_selectionAnchor = QPersistentModelIndex(index);
}

void VirtualItemView::toggleClickedIndex(const QModelIndex &index)
{
    if (!m_selectionModel || !index.isValid())
        return;
    // Toggle and keep the rest of the selection, like QAbstractItemView. The
    // command carries the SelectionBehavior, so SelectRows toggles the whole row
    // instead of a single cell.
    const bool wasSelected = m_selectionModel->isSelected(index);
    const QItemSelectionModel::SelectionFlags command = selectionFlagsFor(
        wasSelected ? QItemSelectionModel::Deselect : QItemSelectionModel::Select);
    if (command != QItemSelectionModel::NoUpdate)
        m_selectionModel->select(selectionRange(index, index), command);
    m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current | rowFlags());
    pinCurrentIndex(index);
    m_selectionAnchor = QPersistentModelIndex(index);
}

void VirtualItemView::extendSelectionTo(const QModelIndex &index)
{
    if (!m_selectionModel || !index.isValid())
        return;
    // QItemSelectionModel has no "extend" flag, so the range is built from the
    // anchor explicitly.
    if (!m_selectionAnchor.isValid() || viewItemForIndex(m_selectionAnchor) < 0) {
        const QModelIndex current = m_selectionModel->currentIndex();
        m_selectionAnchor = QPersistentModelIndex(
            current.isValid() && viewItemForIndex(current) >= 0 ? current : index);
    }
    const QModelIndex base = m_selectionAnchor.isValid() ? QModelIndex(m_selectionAnchor) : index;
    const QItemSelectionModel::SelectionFlags command = selectionFlagsFor(QItemSelectionModel::ClearAndSelect);
    m_selectionModel->select(selectionRange(base, index), command);
    m_selectionModel->setCurrentIndex(index, QItemSelectionModel::Current | rowFlags());
    pinCurrentIndex(index);
}

// ---------------------------------------------------------------------------
// Model signals
// ---------------------------------------------------------------------------

void VirtualItemView::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                                    const QVector<int> &roles)
{
    Q_UNUSED(roles);
    if (!topLeft.isValid() || !bottomRight.isValid())
        return;
    if (topLeft.parent() != bottomRight.parent())
        return;
    if (managesVisibleRows()) {
        if (viewItemForIndex(topLeft) < 0)
            return;
    } else if (!isLayoutParent(topLeft.parent())) {
        return;
    }

    // Identities are unchanged: rebind only the affected materialized items.
    rebindItemsInRange(topLeft, bottomRight);
    // Item sizes may have changed, so keep the visual position stable.
    setPendingAnchor(captureAnchor());
    markDirty();
}

void VirtualItemView::onRowsAboutToBeInserted(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(first);
    Q_UNUSED(last);
    if (!isLayoutParent(parent))
        return;
    setPendingAnchor(captureAnchor());
}

void VirtualItemView::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    if (!isLayoutParent(parent) || !m_layout)
        return;
    const qsizetype count = qsizetype(last) - qsizetype(first) + 1;
    if (count <= 0)
        return;
    m_layout->insertItems(first, count, estimateItemSize(first));
    markDirty();
}

void VirtualItemView::onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last)
{
    if (!isLayoutParent(parent))
        return;
    setPendingAnchor(captureAnchor());
    // The persistent indexes of the removed rows become invalid right after
    // the removal, so their widgets must be recycled now.
    recycleItemsInModelRange(parent, first, last);
}

void VirtualItemView::onRowsRemoved(const QModelIndex &parent, int first, int last)
{
    if (!isLayoutParent(parent) || !m_layout)
        return;
    const qsizetype count = qsizetype(last) - qsizetype(first) + 1;
    if (count > 0)
        m_layout->removeItems(first, count);
    markDirty();
}

void VirtualItemView::onRowsAboutToBeMoved(const QModelIndex &sourceParent, int start, int end,
                                              const QModelIndex &destParent, int row)
{
    Q_UNUSED(sourceParent);
    Q_UNUSED(start);
    Q_UNUSED(end);
    Q_UNUSED(destParent);
    Q_UNUSED(row);
    // Rows keep their identity: the QPersistentModelIndex of every materialized
    // item follows the move, so only geometry has to be refreshed.
}

void VirtualItemView::onRowsMoved(const QModelIndex &sourceParent, int start, int end,
                                     const QModelIndex &destParent, int row)
{
    if (!isLayoutParent(sourceParent) || !isLayoutParent(destParent)) {
        // Cross-parent moves change the visible mapping entirely.
        if (managesVisibleRows()) {
            markDirty();
            return;
        }
        recycleAllItems();
        resetLayoutForNewModel();
        markDirty();
        return;
    }
    if (!m_layout)
        return;
    const qsizetype count = qsizetype(end) - qsizetype(start) + 1;
    if (count > 0)
        m_layout->moveItems(start, count, row);
    markDirty();
}

void VirtualItemView::onLayoutAboutToBeChanged(const QList<QPersistentModelIndex> &parents,
                                                  QAbstractItemModel::LayoutChangeHint hint)
{
    Q_UNUSED(parents);
    Q_UNUSED(hint);
    setPendingAnchor(captureAnchor());
    // The mapping between rows and items may have been rewritten completely:
    // rebuild the materialized set from scratch.
    if (!managesVisibleRows())
        recycleAllItems();
}

void VirtualItemView::onLayoutChanged(const QList<QPersistentModelIndex> &parents,
                                         QAbstractItemModel::LayoutChangeHint hint)
{
    Q_UNUSED(parents);
    Q_UNUSED(hint);
    // Sizes are keyed by row, so they cannot be trusted after a reorder.
    if (!managesVisibleRows())
        resetLayoutForNewModel();
    markDirty();
}

void VirtualItemView::onModelAboutToBeReset()
{
    clearVisualTransitions();
    m_hoveredIndex = QPersistentModelIndex();
    recycleAllItems();
    m_explicitPinned.clear();
    cancelPendingAnchor();
}

void VirtualItemView::onModelReset()
{
    m_scrollOffset = 0;
    m_scrollMapper.resetAnchor();
    if (!managesVisibleRows())
        resetLayoutForNewModel();
    markDirty();
}

} // namespace viv
