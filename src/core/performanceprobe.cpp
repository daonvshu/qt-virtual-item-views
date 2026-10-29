#include "performanceprobe_p.h"

#include <virtualitemviews/virtualitemview.h>

#include <QAtomicInteger>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QScopedValueRollback>
#include <QTimer>
#include <QVector>
#include <algorithm>

Q_LOGGING_CATEGORY(vivPerformanceRefresh, "viv.performance.refresh")
Q_LOGGING_CATEGORY(vivPerformancePaint, "viv.performance.paint")
Q_LOGGING_CATEGORY(vivPerformanceDetails, "viv.performance.details", QtInfoMsg)

namespace viv {
namespace detail {

namespace {
constexpr int kPerformanceLogIntervalMs = 1000;
constexpr int kPerformanceTopStages = 8;
thread_local bool suppressPerformanceProbes = false;
}

struct PerformanceScope::Batch
{
    struct Part {
        const char *name;
        qint64 calls = 0;
        qint64 total = 0;
        qint64 maximum = 0;
        bool counter = false;
    };
    Batch *previous = nullptr;
    QPointer<VirtualItemView> view;
    const VirtualItemView *viewKey = nullptr;
    PerformanceDomain domain;
    quint64 id = 0;
    QByteArray viewType;
    QString viewName;
    VirtualViewStats before;
    int overscanBefore = 0;
    int overscanAfter = 0;
    bool autoMeasure = false;
    QElapsedTimer clock;
    QVector<Part> parts;
    qint64 total = 0;
    qint64 maximum = 0;
    qint64 samples = 0;
    int activeRoots = 0;
    bool flushScheduled = false;
    bool discarded = false;

    // Windows contain only measured work, never time waiting for the reporting timer.
    using Pending = QHash<const VirtualItemView *, std::weak_ptr<Batch>>;
    static thread_local Pending pendingRefresh;
    static thread_local Pending pendingPaint;

    static Pending &pending(PerformanceDomain domain)
    {
        return domain == PerformanceDomain::Paint ? pendingPaint : pendingRefresh;
    }

    static void scheduleFlush(const std::shared_ptr<Batch> &batch)
    {
        if (batch->discarded || batch->flushScheduled || !QCoreApplication::instance())
            return;
        batch->flushScheduled = true;
        const int delay = int(qMax<qint64>(0, kPerformanceLogIntervalMs - batch->clock.elapsed()));
        QTimer::singleShot(delay, QCoreApplication::instance(), [batch]() {
            batch->flushScheduled = false;
            // A callback or renderer may pump events while measured work is active.
            // Its outer scope schedules the completed window after leaving the stack.
            if (batch->activeRoots > 0)
                return;
            auto &windows = pending(batch->domain);
            if (windows.value(batch->viewKey).lock() == batch)
                windows.remove(batch->viewKey);
            if (!batch->discarded)
                batch->write();
        });
    }

    void write() const
    {
        // Do not recursively profile a message handler or a custom stats() implementation.
        QScopedValueRollback<bool> suppress(suppressPerformanceProbes, true);
        if (!view || !view->isPerformanceLoggingEnabled() || discarded)
            return;
        const auto category = domain == PerformanceDomain::Paint
            ? vivPerformancePaint : vivPerformanceRefresh;
        if (!category().isDebugEnabled())
            return;
        const VirtualViewStats after = view->stats();
        if (!view || !view->isPerformanceLoggingEnabled() || discarded)
            return;
        QVector<const Part *> timings;
        QVector<const Part *> counters;
        for (const Part &part : parts) {
            if (part.counter)
                counters.append(&part);
            else
                timings.append(&part);
        }
        std::sort(timings.begin(), timings.end(), [](const Part *left, const Part *right) {
            if (left->total != right->total)
                return left->total > right->total;
            return qstrcmp(left->name, right->name) < 0;
        });
        std::sort(counters.begin(), counters.end(), [](const Part *left, const Part *right) {
            return qstrcmp(left->name, right->name) < 0;
        });
        const double windowMs = clock.nsecsElapsed() / 1000000.0;
        const auto milliseconds = [](qint64 nanoseconds) {
            return QString::number(nanoseconds / 1000000.0, 'f', 3);
        };
        QString message = QStringLiteral("[viv perf #%1] %2 | %3")
            .arg(id)
            .arg(domain == PerformanceDomain::Paint ? QStringLiteral("paint") : QStringLiteral("refresh"))
            .arg(QString::fromLatin1(viewType));
        if (!viewName.isEmpty()) {
            QString quotedName;
            {
                QDebug name(&quotedName);
                name.nospace() << viewName;
            }
            message += QStringLiteral(" | name=") + quotedName;
        }
        message += QStringLiteral("\n  Window: %1 ms | Measured: %2 ms | Samples: %3 | Avg: %4 ms | Max: %5 ms")
            .arg(QString::number(windowMs, 'f', 1)).arg(milliseconds(total)).arg(samples)
            .arg(QString::number(samples > 0 ? total / double(samples) / 1000000.0 : 0.0, 'f', 3))
            .arg(milliseconds(maximum));
        message += QStringLiteral("\n  Widgets: %1 materialized / %2 logical | Pool: %3 | Pinned: %4")
            .arg(after.materializedItems).arg(after.logicalItems).arg(after.pooledWidgets).arg(after.pinnedWidgets);
        message += QStringLiteral("\n  Activity: created=%1 | bound=%2 | recycled=%3 | Overscan: %4/%5 | Auto measure: %6")
            .arg(qint64(after.createCount) - qint64(before.createCount))
            .arg(qint64(after.bindCount) - qint64(before.bindCount))
            .arg(qint64(after.recycleCount) - qint64(before.recycleCount))
            .arg(overscanBefore).arg(overscanAfter)
            .arg(autoMeasure ? QStringLiteral("on") : QStringLiteral("off"));

        int nameWidth = 32;
        for (const Part &part : parts)
            nameWidth = qMax(nameWidth, int(qstrlen(part.name)));
        const auto timingRow = [nameWidth](const QString &name, const QString &sum,
                                          const QString &share, const QString &calls,
                                          const QString &average, const QString &maximum) {
            return QStringLiteral("\n    %1 %2 %3 %4 %5 %6")
                .arg(name, -nameWidth).arg(sum, 10).arg(share, 8)
                .arg(calls, 10).arg(average, 10).arg(maximum, 10);
        };
        const auto appendTimings = [&](QString &output, int limit) {
            output += timingRow(QStringLiteral("Stage"), QStringLiteral("Total ms"), QStringLiteral("Share %"),
                                QStringLiteral("Calls"), QStringLiteral("Avg ms"), QStringLiteral("Max ms"));
            for (int i = 0; i < limit; ++i) {
                const Part &part = *timings.at(i);
                output += timingRow(QString::fromLatin1(part.name), milliseconds(part.total),
                    QString::number(total > 0 ? part.total * 100.0 / double(total) : 0.0, 'f', 1),
                    QString::number(part.calls),
                    QString::number(part.calls > 0 ? part.total / double(part.calls) / 1000000.0 : 0.0, 'f', 3),
                    milliseconds(part.maximum));
            }
        };
        const int shown = qMin(kPerformanceTopStages, int(timings.size()));
        message += QStringLiteral("\n  Hot stages: %1/%2, sorted by total (inclusive; nested stages overlap)")
            .arg(shown).arg(timings.size());
        appendTimings(message, shown);
        if (!timings.isEmpty()) {
            const Part *slowest = *std::max_element(timings.cbegin(), timings.cend(),
                [](const Part *left, const Part *right) { return left->maximum < right->maximum; });
            message += QStringLiteral("\n  Slowest stage call: %1 (%2 ms)")
                .arg(QString::fromLatin1(slowest->name)).arg(milliseconds(slowest->maximum));
        }
        if (!counters.isEmpty()) {
            message += QStringLiteral("\n  Counters (window totals):");
            for (const Part *part : counters) {
                message += QStringLiteral("\n    %1 %2")
                    .arg(QString::fromLatin1(part->name), -nameWidth).arg(part->calls, 10);
            }
        }
        if (shown < timings.size())
            message += QStringLiteral("\n  Full timing table: enable viv.performance.details.debug=true");

        // One handler invocation per summary; formatting happens only when the window closes.
        qCDebug(category).noquote().nospace() << message;
        if (!view || !view->isPerformanceLoggingEnabled() || discarded
            || !category().isDebugEnabled() || !vivPerformanceDetails().isDebugEnabled())
            return;
        QString details = QStringLiteral("[viv perf #%1] %2 | %3 | Full timing table (inclusive; do not add rows)")
            .arg(id)
            .arg(domain == PerformanceDomain::Paint ? QStringLiteral("paint") : QStringLiteral("refresh"))
            .arg(QString::fromLatin1(viewType));
        appendTimings(details, int(timings.size()));
        qCDebug(vivPerformanceDetails).noquote().nospace() << details;
    }

    // Stage names have static storage; compare their text across translation units.
    void record(const char *name, qint64 calls, qint64 elapsed, bool counter = false)
    {
        for (Part &part : parts) {
            if (part.counter != counter || qstrcmp(part.name, name) != 0)
                continue;
            part.calls += calls;
            part.total += elapsed;
            part.maximum = qMax(part.maximum, elapsed);
            return;
        }
        parts.append(Part{name, calls, elapsed, elapsed, counter});
    }
};

thread_local PerformanceScope::Batch *PerformanceScope::s_current = nullptr;
QAtomicInteger<quint32> PerformanceScope::s_enabledViews(0);
thread_local PerformanceScope::Batch::Pending PerformanceScope::Batch::pendingRefresh;
thread_local PerformanceScope::Batch::Pending PerformanceScope::Batch::pendingPaint;

void PerformanceScope::start(const QObject *owner, PerformanceDomain domain)
{
    if (suppressPerformanceProbes)
        return;
    const VirtualItemView *view = nullptr;
    for (const QObject *candidate = owner; candidate; candidate = candidate->parent()) {
        view = qobject_cast<const VirtualItemView *>(candidate);
        if (view)
            break;
    }
    if (!view || !view->isPerformanceLoggingEnabled())
        return;
    start(view, domain);
}

void PerformanceScope::start(const VirtualItemView *view, PerformanceDomain domain)
{
    if (suppressPerformanceProbes || !view->isPerformanceLoggingEnabled())
        return;
    if (domain == PerformanceDomain::Current) {
        domain = PerformanceDomain::Refresh;
        for (Batch *batch = s_current; batch; batch = batch->previous) {
            if (!batch->discarded && batch->view.data() == view) {
                domain = batch->domain;
                break;
            }
        }
    }
    const QLoggingCategory &category = domain == PerformanceDomain::Paint
        ? vivPerformancePaint() : vivPerformanceRefresh();
    if (!category.isDebugEnabled())
        return;
    for (Batch *batch = s_current; batch; batch = batch->previous) {
        if (!batch->discarded && batch->view.data() == view && batch->domain == domain) {
            m_batch = batch;
            break;
        }
    }
    if (!m_batch) {
        static QAtomicInteger<quint32> nextId(0);
        m_owned = Batch::pending(domain).value(view).lock();
        if (m_owned && (m_owned->discarded || m_owned->view.data() != view))
            m_owned.reset();
        if (!m_owned) {
            m_owned.reset(new Batch);
            m_owned->view = const_cast<VirtualItemView *>(view);
            m_owned->viewKey = view;
            m_owned->domain = domain;
            m_owned->id = nextId.fetchAndAddRelaxed(1) + 1;
            m_owned->viewType = view->metaObject()->className();
            m_owned->viewName = view->objectName();
            m_owned->overscanBefore = view->overscanBefore();
            m_owned->overscanAfter = view->overscanAfter();
            m_owned->autoMeasure = view->autoMeasureItemHeight();
            {
                QScopedValueRollback<bool> suppress(suppressPerformanceProbes, true);
                m_owned->before = view->stats();
            }
            // stats() is virtual and may disable probing or destroy the view.
            if (!m_owned->view || !m_owned->view->isPerformanceLoggingEnabled()
                || !category.isDebugEnabled()) {
                m_owned.reset();
                return;
            }
            m_owned->clock.start();
            Batch::pending(domain).insert(view, m_owned);
        }
        m_batch = m_owned.get();
        m_batch->previous = s_current;
        ++m_batch->activeRoots;
        s_current = m_batch;
    }
    m_start = m_batch->clock.nsecsElapsed();
    m_checkpoint = m_start;
}

void PerformanceScope::finish()
{
    if (!m_owned) {
        if (!m_batch->discarded)
            m_batch->record(m_stage, 1, m_batch->clock.nsecsElapsed() - m_start);
        return;
    }
    // Restore the active chain before queuing any reporting work.
    s_current = m_batch->previous;
    m_batch->previous = nullptr;
    --m_batch->activeRoots;
    if (m_batch->discarded)
        return;
    const qint64 elapsed = m_batch->clock.nsecsElapsed() - m_start;
    m_batch->total += elapsed;
    m_batch->maximum = qMax(m_batch->maximum, elapsed);
    ++m_batch->samples;
    m_batch->record(m_stage, 1, elapsed);
    Batch::scheduleFlush(m_owned);
}

void PerformanceScope::recordCount(const char *name, qint64 amount)
{
    if (m_batch && !m_batch->discarded)
        m_batch->record(name, amount, 0, true);
}

void PerformanceScope::recordCheckpoint(const char *stage)
{
    if (!m_batch || m_batch->discarded)
        return;
    const qint64 now = m_batch->clock.nsecsElapsed();
    m_batch->record(stage, 1, now - m_checkpoint);
    m_checkpoint = now;
}

void PerformanceScope::viewLoggingChanged(bool enabled)
{
    if (enabled)
        s_enabledViews.fetchAndAddRelaxed(1);
    else
        s_enabledViews.fetchAndSubRelaxed(1);
}

void PerformanceScope::discard(const VirtualItemView *view)
{
    const auto discardFrom = [view](Batch::Pending &windows) {
        const auto it = windows.find(view);
        if (it == windows.end())
            return;
        if (const auto batch = it.value().lock())
            batch->discarded = true;
        windows.erase(it);
    };
    discardFrom(Batch::pendingRefresh);
    discardFrom(Batch::pendingPaint);
}

} // namespace detail
} // namespace viv
