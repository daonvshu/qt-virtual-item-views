#pragma once

#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/layoutpolicy.h>

#include <QMargins>

namespace viv {

/// Linear (list) layout: items are stacked along one axis.
///
/// The layout owns a SizeIndex, which decouples the layout from the actual
/// size model: FixedSizeIndex for fixed-height content, BlockSizeIndex for
/// dynamic heights.
class ListLayout : public LayoutPolicy
{
public:
    explicit ListLayout(Qt::Orientation orientation = Qt::Vertical);
    ListLayout(SizeIndex *sizeIndex, Qt::Orientation orientation = Qt::Vertical);
    ~ListLayout() override;

    Qt::Orientation orientation() const override { return m_orientation; }
    void setOrientation(Qt::Orientation orientation);

    SizeIndex *sizeIndex() const { return m_sizeIndex; }
    /// Replaces the size index. The previous owned index is deleted.
    void setSizeIndex(SizeIndex *sizeIndex, bool takeOwnership = true) override;

    qsizetype itemCount() const override;
    int itemSize(qsizetype item) const override;
    qint64 offsetOf(qsizetype item) const override;
    qsizetype indexAtOffset(qint64 offset) const override;
    qint64 contentExtent() const override;

    int crossExtent() const override { return m_crossExtent; }
    void setCrossExtent(int extent) override;

    QRect itemRect(qsizetype item, qint64 scrollOffset) const override;
    qsizetype itemAtPoint(const QPoint &viewportPos, qint64 scrollOffset) const override;

    bool isVariableSized() const override;

    void resetItems(qsizetype count, int estimate) override;
    void insertItems(qsizetype index, qsizetype count, int estimate) override;
    void removeItems(qsizetype index, qsizetype count) override;
    void moveItems(qsizetype from, qsizetype count, qsizetype to) override;
    void setItemSize(qsizetype item, int size) override;

    /// Estimated item size used when a caller passes estimate <= 0.
    int estimate() const { return m_estimate; }
    void setEstimate(int estimate);

    /// Margins added around the item area (unused by the kernel in v0.1).
    QMargins viewportMargins() const { return m_viewportMargins; }
    void setViewportMargins(const QMargins &margins);

private:
    qint64 clampToIntRange(qint64 value) const;

    SizeIndex *m_sizeIndex = nullptr;
    bool m_ownsSizeIndex = true;
    Qt::Orientation m_orientation = Qt::Vertical;
    int m_crossExtent = 0;
    int m_estimate = 0;
    QMargins m_viewportMargins;
};

} // namespace viv
