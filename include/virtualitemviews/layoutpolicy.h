#pragma once

#include <virtualitemviews/global.h>

#include <QPoint>
#include <QRect>
#include <Qt>
#include <QtGlobal>

namespace viv {

class SizeIndex;

/// Geometry strategy of a virtual view.
///
/// The kernel only talks to this interface: it never assumes that items are
/// rows or that the scrolling axis is vertical. List/Table/Tree therefore share
/// one virtualization kernel and differ in their layout/visibility strategy.
class VIRTUALITEMVIEWS_EXPORT LayoutPolicy
{
public:
    virtual ~LayoutPolicy() = default;

    virtual Qt::Orientation orientation() const = 0;

    virtual qsizetype itemCount() const = 0;

    /// Item extent along the scrolling axis.
    virtual int itemSize(qsizetype item) const = 0;

    /// Offset of the item start along the scrolling axis.
    virtual qint64 offsetOf(qsizetype item) const = 0;

    /// First item intersecting \a offset; may return itemCount().
    virtual qsizetype indexAtOffset(qint64 offset) const = 0;

    /// Total extent along the scrolling axis.
    virtual qint64 contentExtent() const = 0;

    /// Extent along the cross axis (viewport width for a vertical list).
    virtual int crossExtent() const = 0;
    virtual void setCrossExtent(int extent) = 0;

    /// Geometry of an item in viewport coordinates for a given scroll offset.
    virtual QRect itemRect(qsizetype item, qint64 scrollOffset) const = 0;

    /// Item under a viewport position, or -1 when the position hits no item.
    virtual qsizetype itemAtPoint(const QPoint &viewportPos, qint64 scrollOffset) const = 0;

    /// True when item sizes may differ from each other.
    virtual bool isVariableSized() const = 0;

    /// Size model of the layout, when the layout is index based (rows/columns).
    virtual SizeIndex *sizeIndex() const { return nullptr; }
    /// Replaces the size model. An index based layout (ListLayout) takes
    /// ownership and uses it; a policy that has no size index at all - the
    /// default - releases what it was told to own, so handing a policy an index
    /// can never leak it (see docs/api-stability.md: no "accepted but ignored"
    /// entry points).
    virtual void setSizeIndex(SizeIndex *index, bool takeOwnership = true);

    /// Mutation hooks used by the kernel.
    virtual void resetItems(qsizetype count, int estimate) = 0;
    virtual void insertItems(qsizetype index, qsizetype count, int estimate) = 0;
    virtual void removeItems(qsizetype index, qsizetype count) = 0;
    virtual void moveItems(qsizetype from, qsizetype count, qsizetype to) = 0;
    virtual void setItemSize(qsizetype item, int size) = 0;
};

} // namespace viv
