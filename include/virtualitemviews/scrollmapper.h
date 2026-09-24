#pragma once

#include <QtGlobal>

namespace viv {

/// Maps between the 64-bit logical scroll space and the int-based QScrollBar
/// range.
///
/// The logical content extent of a virtual view may exceed INT_MAX pixels
/// (millions of dynamic-height rows). QScrollBar only offers an int range, so
/// this mapper compresses the logical space into the scrollbar range.
///
/// Compression is *windowed*: the mapper keeps an anchor pair
/// (logicalOffset, scrollBarValue) that corresponds to the current viewport
/// position. Conversions close to the anchor are therefore at least as precise
/// as the scrollbar step itself, and value -> offset -> value round trips are
/// exact. This is what keeps the scrollbar stable (no thumb jitter) while
/// dragging and while the height of the content changes.
class ScrollMapper
{
public:
    /// Largest scrollbar value span used when the content does not fit into an
    /// int range. Kept below INT_MAX to leave room for Qt's own arithmetic.
    static constexpr int kMaxScrollRange = 2000000000;

    /// Sets the logical content extent and the viewport extent (both in pixels
    /// along the scrolling axis). The anchor is kept as-is.
    void setExtents(qint64 contentExtent, int viewportExtent);

    qint64 contentExtent() const { return m_contentExtent; }
    int viewportExtent() const { return m_viewportExtent; }

    /// Largest valid logical offset: contentExtent - viewportExtent, floored at 0.
    qint64 maximumOffset() const;

    /// Largest valid scrollbar value (that is: scrollBarMaximum - scrollBarMinimum).
    int scrollRange() const;

    /// True when the logical space is compressed into the scrollbar range.
    bool isScaled() const;

    int toScrollBarValue(qint64 offset) const;
    qint64 toLogicalOffset(int value) const;

    /// Names used by the architecture document.
    int toScrollbar(qint64 logicalOffset) const { return toScrollBarValue(logicalOffset); }
    qint64 toLogical(int scrollbarValue) const { return toLogicalOffset(scrollbarValue); }

    /// Re-centres the compression window. Call this whenever the viewport
    /// position changes (scrollbar drag, wheel, keyboard, programmatic scroll)
    /// so that precision stays maximal where the user is looking.
    void setAnchor(qint64 offset, int value);
    void resetAnchor();

    qint64 anchorOffset() const { return m_anchorOffset; }
    int anchorValue() const { return m_anchorValue; }

private:
    double scale() const;

    qint64 m_contentExtent = 0;
    int m_viewportExtent = 0;
    qint64 m_anchorOffset = 0;
    int m_anchorValue = 0;
};

} // namespace viv
