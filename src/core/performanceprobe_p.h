#pragma once

#include <virtualitemviews/virtualitemview.h>
#include <QAtomicInteger>
#include <memory>

class QObject;

namespace viv {
class VirtualItemView;
namespace detail {

enum class PerformanceDomain { Current, Refresh, Paint };

// Nested scopes aggregate into a timed reporting window for the same view and domain.
// Disabled probes skip owner lookup, timing, aggregation and reporting work.
class VIRTUALITEMVIEWS_EXPORT PerformanceScope
{
public:
    PerformanceScope(const QObject *owner, const char *stage,
                     PerformanceDomain domain = PerformanceDomain::Current)
        : m_stage(stage)
    {
        if (mayRecord(owner))
            start(owner, domain);
    }
    PerformanceScope(const VirtualItemView *view, const char *stage,
                     PerformanceDomain domain = PerformanceDomain::Current)
        : m_stage(stage)
    {
        if (mayRecord(view))
            start(view, domain);
    }
    ~PerformanceScope()
    {
        if (m_batch)
            finish();
    }
    PerformanceScope(const PerformanceScope &) = delete;
    PerformanceScope &operator=(const PerformanceScope &) = delete;

    // Keep disabled callback probes out of scope construction entirely.
    static bool mayRecord(const QObject *owner)
    {
        return owner && s_enabledViews.loadRelaxed() != 0;
    }
    static bool mayRecord(const VirtualItemView *view)
    {
        return view && view->isPerformanceLoggingEnabled();
    }
    bool isActive() const { return m_batch != nullptr; }
    void count(const char *name, qint64 amount = 1)
    {
        if (m_batch)
            recordCount(name, amount);
    }
    void checkpoint(const char *stage)
    {
        if (m_batch)
            recordCheckpoint(stage);
    }
    // Balanced by the view's setter and destructor; no per-widget registration.
    static void viewLoggingChanged(bool enabled);
    // Drop pending windows without logging when probing stops or the view is destroyed.
    static void discard(const VirtualItemView *view);

private:
    struct Batch;
    void start(const QObject *owner, PerformanceDomain domain);
    void start(const VirtualItemView *view, PerformanceDomain domain);
    void finish();
    void recordCount(const char *name, qint64 amount);
    void recordCheckpoint(const char *stage);
    static QAtomicInteger<quint32> s_enabledViews;
    static thread_local Batch *s_current;
    std::shared_ptr<Batch> m_owned;
    Batch *m_batch = nullptr;
    const char *m_stage;
    qint64 m_start = 0;
    qint64 m_checkpoint = 0;
};

// Includes all synchronous model access, signals and widget work inside a callback.
template <typename Owner, typename Function>
decltype(auto) performanceCall(const Owner *owner, const char *stage, Function &&function,
                               PerformanceDomain domain = PerformanceDomain::Current)
{
    if (!PerformanceScope::mayRecord(owner))
        return function();
    PerformanceScope scope(owner, stage, domain);
    return function();
}

} // namespace detail
} // namespace viv
