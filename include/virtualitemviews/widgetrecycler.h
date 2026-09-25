#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/types.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QtGlobal>

#include <functional>

class QWidget;

namespace viv {

/// Pool of QWidget instances grouped by WidgetType.
///
/// The recycler is the only place where item widgets are created or destroyed.
/// Scrolling must therefore only bind/recycle widgets: steady-state scrolling
/// performs no allocation at all.
class VIRTUALITEMVIEWS_EXPORT WidgetRecycler : public QObject
{
    Q_OBJECT

public:
    /// Creates a widget of \a type parented to \a parent.
    using Factory = std::function<QWidget *(WidgetType type, QWidget *parent)>;

    /// Default upper bound of one pool, used by trim().
    static constexpr qsizetype kDefaultMaxPoolSize = 256;

    explicit WidgetRecycler(QObject *parent = nullptr);
    ~WidgetRecycler() override;

    WidgetRecycler(const WidgetRecycler &) = delete;
    WidgetRecycler &operator=(const WidgetRecycler &) = delete;

    void setParentWidget(QWidget *parent);
    QWidget *parentWidget() const { return m_parent; }

    void setFactory(Factory factory);

    /// Returns a widget of \a type: a pooled one when available, otherwise a
    /// newly created one. Returns nullptr when the factory fails.
    /// The returned widget is hidden; the caller binds it and shows it.
    QWidget *acquire(WidgetType type);

    /// Returns \a widget to the pool of \a type. The widget is hidden; when the
    /// pool is full the widget is scheduled for deletion (deleteLater()).
    void recycle(WidgetType type, QWidget *widget);

    /// Upper bound of the pool of \a type. A negative size means unlimited.
    void setMaxPoolSize(WidgetType type, qsizetype size);
    qsizetype maxPoolSize(WidgetType type) const;

    qsizetype pooledCount(WidgetType type) const;
    qsizetype pooledCount() const;
    qsizetype activeCount() const { return m_activeCount; }

    /// Widgets created by the factory since construction.
    qsizetype createdCount() const { return m_createdCount; }
    /// Widgets actually destroyed (destruction confirmed by QObject::destroyed).
    qsizetype destroyedCount() const { return m_destroyedCount; }
    /// Widgets whose destruction was scheduled through deleteLater().
    qsizetype pendingDestructionCount() const { return m_pendingDestructions; }
    qsizetype acquireCount() const { return m_acquireCount; }
    /// Acquires that were served from a pool instead of creating a widget.
    qsizetype reuseCount() const { return m_reuseCount; }

    /// Widgets that exist right now (materialized + pooled).
    qsizetype aliveCount() const { return m_createdCount - m_destroyedCount; }

    bool isPooled(const QWidget *widget) const;

    /// Deletes pooled widgets above the configured per-type limit.
    void trim();
    /// Deletes every pooled widget immediately.
    void clear();

private:
    bool canReuse(WidgetType type, const QWidget *widget) const;

    QWidget *m_parent = nullptr;
    Factory m_factory;
    QHash<WidgetType, QList<QWidget *>> m_pools;
    QHash<WidgetType, qsizetype> m_maxPoolSize;
    qsizetype m_activeCount = 0;
    qsizetype m_createdCount = 0;
    qsizetype m_destroyedCount = 0;
    qsizetype m_pendingDestructions = 0;
    qsizetype m_acquireCount = 0;
    qsizetype m_reuseCount = 0;
};

} // namespace viv
