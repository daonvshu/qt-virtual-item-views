#include <virtualitemviews/widgetrecycler.h>

#include <QWidget>

#include <algorithm>

namespace viv {

// A shared build has to export this public constant (see virtualitemview.cpp).
namespace {
[[maybe_unused]] const void *const volatile kExportedConstants[] = {
    &WidgetRecycler::kDefaultMaxPoolSize,
};
} // namespace

WidgetRecycler::WidgetRecycler(QObject *parent)
    : QObject(parent)
{
    // The viewport is usually passed as the QObject parent; use it as the pool
    // parent as well so that created widgets are children of the viewport.
    m_parent = qobject_cast<QWidget *>(parent);
}

WidgetRecycler::~WidgetRecycler()
{
    clear();
}

void WidgetRecycler::setParentWidget(QWidget *parent)
{
    m_parent = parent;
}

QWidget *WidgetRecycler::parentWidget() const
{
    return m_parent.data();
}

void WidgetRecycler::setFactory(Factory factory)
{
    m_factory = std::move(factory);
}

void WidgetRecycler::setMaxPoolSize(WidgetType type, qsizetype size)
{
    m_maxPoolSize[type] = size < 0 ? -1 : size;
}

qsizetype WidgetRecycler::maxPoolSize(WidgetType type) const
{
    return m_maxPoolSize.value(type, kDefaultMaxPoolSize);
}

qsizetype WidgetRecycler::pooledCount(WidgetType type) const
{
    qsizetype count = 0;
    for (const QPointer<QWidget> &widget : m_pools.value(type)) {
        if (widget)
            ++count;
    }
    return count;
}

qsizetype WidgetRecycler::pooledCount() const
{
    qsizetype total = 0;
    for (auto it = m_pools.constBegin(); it != m_pools.constEnd(); ++it)
        for (const QPointer<QWidget> &widget : it.value()) {
            if (widget)
                ++total;
        }
    return total;
}

bool WidgetRecycler::isPooled(const QWidget *widget) const
{
    for (auto it = m_pools.constBegin(); it != m_pools.constEnd(); ++it) {
        for (const QPointer<QWidget> &candidate : it.value()) {
            if (candidate.data() == widget)
                return true;
        }
    }
    return false;
}

bool WidgetRecycler::canReuse(WidgetType type, const QWidget *widget) const
{
    // Defense in depth: a widget that is still visible, has focus or a popup
    // must not be handed out again without going through the kernel.
    if (widget->isVisible())
        return false;
    if (widget->hasFocus())
        return false;
    Q_UNUSED(type);
    return true;
}

QWidget *WidgetRecycler::acquire(WidgetType type)
{
    ++m_acquireCount;

    QList<QPointer<QWidget>> &pool = m_pools[type];
    while (!pool.isEmpty()) {
        QWidget *candidate = pool.takeLast().data();
        if (!candidate)
            continue;
        if (candidate->parentWidget() != m_parent.data())
            candidate->setParent(m_parent.data());
        if (!canReuse(type, candidate)) {
            // Keep it out of the pool until the kernel recycles it properly.
            candidate->hide();
        }
        ++m_reuseCount;
        ++m_activeCount;
        return candidate;
    }

    if (!m_factory)
        return nullptr;

    QWidget *created = m_factory(type, m_parent.data());
    if (!created)
        return nullptr;
    ++m_createdCount;
    ++m_activeCount;
    QObject::connect(created, &QObject::destroyed, this, [this]() { ++m_destroyedCount; });
    return created;
}

void WidgetRecycler::recycle(WidgetType type, QWidget *widget)
{
    if (!widget)
        return;

    if (m_activeCount > 0)
        --m_activeCount;

    widget->hide();
    widget->clearFocus();
    if (widget->parentWidget() != m_parent.data())
        widget->setParent(m_parent.data());

    QList<QPointer<QWidget>> &pool = m_pools[type];
    pool.erase(std::remove_if(pool.begin(), pool.end(),
                              [](const QPointer<QWidget> &candidate) { return !candidate; }),
               pool.end());
    const qsizetype limit = maxPoolSize(type);
    if (limit >= 0 && pool.size() >= limit) {
        ++m_pendingDestructions;
        widget->deleteLater();
        return;
    }
    pool.append(widget);
}

void WidgetRecycler::trim()
{
    for (auto it = m_pools.begin(); it != m_pools.end(); ++it) {
        const qsizetype limit = maxPoolSize(it.key());
        if (limit < 0)
            continue;
        QList<QPointer<QWidget>> &pool = it.value();
        pool.erase(std::remove_if(pool.begin(), pool.end(),
                                  [](const QPointer<QWidget> &candidate) { return !candidate; }),
                   pool.end());
        while (pool.size() > limit) {
            QWidget *widget = pool.takeLast().data();
            if (!widget)
                continue;
            ++m_pendingDestructions;
            widget->deleteLater();
        }
    }
}

void WidgetRecycler::clear()
{
    for (auto it = m_pools.begin(); it != m_pools.end(); ++it) {
        for (const QPointer<QWidget> &candidate : it.value()) {
            QWidget *widget = candidate.data();
            if (!widget)
                continue;
            ++m_pendingDestructions;
            delete widget;
        }
        it.value().clear();
    }
}

} // namespace viv
