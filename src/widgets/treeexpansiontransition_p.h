#pragma once

#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/listlayout.h>
#include <virtualitemviews/treevisibilityindex.h>
#include <virtualitemviews/virtualitemview.h>
#include <QEvent>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QVariantAnimation>
#include <functional>
#include <utility>

namespace viv {

// Drives live layout geometry; never captures pixels or covers the viewport.
class TreeExpansionTransition : public QObject
{
public:
    TreeExpansionTransition(VirtualItemView *view, ListLayout *layout,
                            std::function<void()> refreshLayout, HeaderGeometry *header = nullptr)
        : QObject(view), m_view(view), m_layout(layout), m_refresh(std::move(refreshLayout)),
          m_header(header)
    {
        setObjectName(QStringLiteral("vivTreeExpansionTransition"));
        m_animation.setObjectName(QStringLiteral("vivTreeExpansionAnimation"));
        m_animation.setParent(this);
        m_animation.setEasingCurve(QEasingCurve::OutCubic);
        view->installEventFilter(this);
        view->viewport()->installEventFilter(this);
        view->verticalScrollBar()->installEventFilter(this);
        connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            if (m_layout->hasTransientGeometry()) {
                const qreal progress = value.toReal();
                m_layout->m_transitionExtent = qRound64(m_startExtent + (m_endExtent - m_startExtent) * progress);
                m_layout->m_transitionOpacity = m_startOpacity + (m_endOpacity - m_startOpacity) * progress;
                refresh();
            }
        });
        connect(&m_animation, &QVariantAnimation::finished, this, [this]() { stop(); });
    }

    ~TreeExpansionTransition() override { cancel(); }

    void watchModel(QAbstractItemModel *model)
    {
        if (m_model)
            disconnect(m_model, nullptr, this, nullptr);
        cancel();
        m_model = model;
        if (!model)
            return;
        // Never run expansion callbacks inside a structural model mutation.
        const auto abort = [this]() { cancel(); };
        connect(model, &QAbstractItemModel::modelAboutToBeReset, this, abort);
        connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, abort);
        connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, abort);
        connect(model, &QAbstractItemModel::rowsAboutToBeMoved, this, abort);
        connect(model, &QAbstractItemModel::columnsAboutToBeInserted, this, abort);
        connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, this, abort);
        connect(model, &QAbstractItemModel::columnsAboutToBeMoved, this, abort);
        connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this, abort);
        connect(model, &QObject::destroyed, this, abort);
        connect(model, &QAbstractItemModel::dataChanged, this, [this]() { stop(); });
    }

    bool isEnabled() const { return m_enabled; }
    bool isFadeEnabled() const { return m_layout->m_transitionFadeEnabled; }
    void setFadeEnabled(bool enabled)
    {
        if (isFadeEnabled() == enabled)
            return;
        m_layout->m_transitionFadeEnabled = enabled;
        if (m_layout->hasTransientGeometry())
            refresh();
    }
    bool isUpdatingGeometry() const { return m_refreshDepth > 0; }
    void setEnabled(bool enabled)
    {
        m_enabled = enabled;
        if (!enabled)
            stop();
    }
    int duration() const { return m_duration; }
    void setDuration(int milliseconds)
    {
        m_duration = qMax(0, milliseconds);
        if (!m_duration)
            stop();
    }
    bool isCollapsing(const QModelIndex &node) const { return m_complete && m_node == node; }
    bool affects(const QModelIndex &node) const
    {
        return m_layout->hasTransientGeometry() && m_node == node;
    }

    // The eased open fraction also drives branch rotation when fading is disabled.
    qreal expansionProgress(const QModelIndex &node, bool expanded) const
    {
        return affects(node) ? m_layout->m_transitionOpacity : (expanded ? 1.0 : 0.0);
    }

    bool resumeExpansion(const QModelIndex &node)
    {
        if (!isCollapsing(node))
            return false;
        m_complete = {};
        start(m_layout->m_transitionFirst, m_layout->m_transitionEnd, true);
        return true;
    }

    // Keep the expanded mapping until the outgoing live widgets finish moving.
    bool collapse(TreeVisibilityIndex *visibility, const QModelIndex &node,
                  std::function<void()> complete)
    {
        if (!canAnimate())
            return false;
        const qsizetype first = visibility->visibleRowForIndex(node) + 1;
        const auto branch = visibility->m_branches.constFind(node);
        if (first <= 0 || branch == visibility->m_branches.constEnd() || branch->total() <= 0)
            return false;
        m_node = node;
        m_complete = std::move(complete);
        start(first, first + branch->total(), false);
        return true;
    }

    void expand(const QModelIndex &node, qsizetype first, qsizetype count)
    {
        if (canAnimate() && first >= 0 && count > 0) {
            m_node = node;
            start(first, first + count, true);
        }
    }

    void stop()
    {
        if (m_finishing || !m_layout->hasTransientGeometry())
            return;
        m_animation.stop();
        clearGeometry();
        auto complete = std::move(m_complete);
        m_complete = {};
        m_node = QModelIndex();
        m_finishing = true;
        const QPointer<TreeExpansionTransition> guard(this);
        if (complete)
            complete();
        if (!guard)
            return;
        m_finishing = false;
        refresh();
    }

    // Call before destroying the view's layout or invalidating model mappings.
    void cancel()
    {
        const bool active = m_layout->hasTransientGeometry();
        m_animation.stop();
        clearGeometry();
        m_complete = {};
        m_node = QModelIndex();
        if (active)
            QTimer::singleShot(0, this, [this]() { refresh(); });
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const bool input = event->type() == QEvent::Wheel
            || (watched == m_view->verticalScrollBar() && event->type() == QEvent::MouseButtonPress);
        if (input || (watched == m_view && (event->type() == QEvent::Resize
                                          || event->type() == QEvent::Hide)))
            stop();
        return false;
    }

private:
    bool canAnimate() const
    {
        return !m_finishing && m_enabled && m_duration > 0 && m_view->isVisible()
            && !m_view->frozenRows() && !m_view->frozenBottomRows();
    }

    void start(qsizetype first, qsizetype end, bool expanding)
    {
        const bool reversing = m_layout->hasTransientGeometry();
        const qint64 current = m_layout->m_transitionExtent;
        const qreal currentOpacity = m_layout->m_transitionOpacity;
        m_animation.stop();
        const qint64 startOffset = m_layout->naturalOffsetOf(first);
        const qint64 natural = m_layout->naturalOffsetOf(end) - startOffset;
        // Once the subtree covers the viewport, its offscreen tail needs no
        // interpolation. Work per frame stays bounded by visible rows.
        const qint64 visible = qMax<qint64>(1, m_view->verticalOffset()
                                            + m_view->viewport()->height() - startOffset);
        const qint64 extent = qMin(natural, visible);
        m_layout->m_transitionFirst = first;
        m_layout->m_transitionEnd = end;
        m_layout->m_transitionExtent = reversing ? current : (expanding ? 0 : extent);
        m_startExtent = qreal(m_layout->m_transitionExtent);
        m_endExtent = qreal(expanding ? extent : 0);
        m_startOpacity = reversing ? currentOpacity : (expanding ? 0.0 : 1.0);
        m_endOpacity = expanding ? 1.0 : 0.0;
        m_layout->m_transitionOpacity = m_startOpacity;
        if (m_header)
            m_header->m_transientRows = m_layout;
        {
            const QSignalBlocker blocker(&m_animation);
            m_animation.setDuration(m_duration);
            m_animation.setStartValue(0.0);
            m_animation.setEndValue(1.0);
        }
        const QPointer<TreeExpansionTransition> guard(this);
        refresh();
        if (guard && m_layout->hasTransientGeometry())
            m_animation.start();
    }

    void clearGeometry()
    {
        m_layout->m_transitionFirst = -1;
        m_layout->m_transitionEnd = -1;
        m_layout->m_transitionOpacity = 1.0;
        if (m_header)
            m_header->m_transientRows = nullptr;
    }

    void refresh()
    {
        const QPointer<TreeExpansionTransition> guard(this);
        ++m_refreshDepth;
        if (m_header)
            emit m_header->geometryChanged();
        if (guard) {
            const auto callback = m_refresh;
            callback();
        }
        if (guard)
            --m_refreshDepth;
    }

    QPointer<VirtualItemView> m_view;
    ListLayout *m_layout;
    std::function<void()> m_refresh;
    QPointer<HeaderGeometry> m_header;
    QPointer<QAbstractItemModel> m_model;
    QPersistentModelIndex m_node;
    std::function<void()> m_complete;
    QVariantAnimation m_animation;
    int m_duration = 300;
    qreal m_startExtent = 0.0;
    qreal m_endExtent = 0.0;
    qreal m_startOpacity = 1.0;
    qreal m_endOpacity = 1.0;
    bool m_enabled = true;
    bool m_finishing = false;
    int m_refreshDepth = 0;
};

} // namespace viv
