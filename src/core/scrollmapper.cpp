#include <virtualitemviews/scrollmapper.h>

#include <QPoint>

#include <cmath>

namespace viv {

// A shared build has to export this public constant (see virtualitemview.cpp).
namespace {
[[maybe_unused]] const void *const kExportedConstants[] = {
    &ScrollMapper::kMaxScrollRange,
};
} // namespace

void ScrollMapper::setExtents(qint64 contentExtent, int viewportExtent)
{
    m_contentExtent = qMax<qint64>(0, contentExtent);
    m_viewportExtent = qMax(0, viewportExtent);
    // Keep the anchor inside the new logical space so that mappings never
    // become inconsistent after a content change.
    m_anchorOffset = qBound<qint64>(qint64(0), m_anchorOffset, maximumOffset());
    m_anchorValue = qBound(0, m_anchorValue, scrollRange());
}

qint64 ScrollMapper::maximumOffset() const
{
    return qMax<qint64>(0, m_contentExtent - qint64(m_viewportExtent));
}

bool ScrollMapper::isScaled() const
{
    return maximumOffset() > kMaxScrollRange;
}

int ScrollMapper::scrollRange() const
{
    const qint64 maxOffset = maximumOffset();
    return maxOffset > kMaxScrollRange ? kMaxScrollRange : int(maxOffset);
}

double ScrollMapper::scale() const
{
    if (!isScaled())
        return 1.0;
    return double(maximumOffset()) / double(kMaxScrollRange);
}

int ScrollMapper::toScrollBarValue(qint64 offset) const
{
    const qint64 clamped = qBound<qint64>(qint64(0), offset, maximumOffset());
    const double factor = scale();
    if (factor == 1.0)
        return int(clamped);

    const double value = double(m_anchorValue) + double(clamped - m_anchorOffset) / factor;
    const qint64 rounded = qint64(std::llround(value));
    return int(qBound<qint64>(qint64(0), rounded, qint64(scrollRange())));
}

qint64 ScrollMapper::toLogicalOffset(int value) const
{
    const qint64 maxOffset = maximumOffset();
    const double factor = scale();
    if (factor == 1.0)
        return qBound<qint64>(qint64(0), qint64(value), maxOffset);

    const int clampedValue = qBound(0, value, scrollRange());
    const double offset = double(m_anchorOffset) + double(clampedValue - m_anchorValue) * factor;
    const qint64 rounded = qint64(std::llround(offset));
    return qBound<qint64>(qint64(0), rounded, maxOffset);
}

void ScrollMapper::setAnchor(qint64 offset, int value)
{
    m_anchorOffset = qBound<qint64>(qint64(0), offset, maximumOffset());
    m_anchorValue = qBound(0, value, scrollRange());
}

void ScrollMapper::resetAnchor()
{
    m_anchorOffset = 0;
    m_anchorValue = 0;
}

} // namespace viv
