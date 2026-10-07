#pragma once

#include <virtualitemviews/layoutpolicy.h>

#include <QMargins>
#include <QSize>
#include <QVector>

namespace viv {

/// Geometry policy for a vertical shortest-column masonry layout.
///
/// The policy keeps compact geometry for every logical item because an item's
/// vertical position depends on all preceding item heights. It does not own
/// widgets and is intentionally independent from VirtualMasonryView.
class MasonryLayout : public LayoutPolicy
{
public:
    enum class HeightMode { Fixed, AspectRatio, Estimated };

    Qt::Orientation orientation() const override { return Qt::Vertical; }
    qsizetype itemCount() const override { return m_count; }
    int itemSize(qsizetype item) const override;
    qint64 offsetOf(qsizetype item) const override;
    qsizetype indexAtOffset(qint64 offset) const override;
    qsizetype lastIndexAtOffset(qint64 offset) const override;
    qint64 contentExtent() const override;
    int crossExtent() const override { return m_width; }
    void setCrossExtent(int extent) override;
    QRect itemRect(qsizetype item, qint64 scrollOffset) const override;
    qsizetype itemAtPoint(const QPoint &point, qint64 scrollOffset) const override;
    bool isVariableSized() const override { return true; }
    bool needsInitialMeasurement() const override { return false; }
    void resetItems(qsizetype count, int estimate) override;
    void insertItems(qsizetype index, qsizetype count, int estimate) override;
    void removeItems(qsizetype index, qsizetype count) override;
    void moveItems(qsizetype from, qsizetype count, qsizetype to) override;
    void setItemSize(qsizetype item, int size) override;

    void setWidthRange(int minimum, int maximum);
    int minimumWidth() const { return m_minimumWidth; }
    int maximumWidth() const { return m_maximumWidth; }
    void setColumnCount(int count);
    int requestedColumnCount() const { return m_requestedColumns; }
    int columnCount() const { return m_columns; }
    int columnWidth(int column) const;
    int columnX(int column) const;
    void setSpacing(int rowSpacing, int columnSpacing);
    int rowSpacing() const { return m_rowSpacing; }
    int columnSpacing() const { return m_columnSpacing; }
    void setMargins(const QMargins &margins);
    QMargins margins() const { return m_margins; }

    void setHeightMode(HeightMode mode);
    HeightMode heightMode() const { return m_heightMode; }
    void setFixedHeight(int height);
    int fixedHeight() const { return m_fixedHeight; }
    void setAspectRatio(qreal widthOverHeight);
    qreal aspectRatio() const { return m_aspectRatio; }
    void setEstimatedSize(qsizetype item, const QSize &size);
    void setEstimatedSizes(const QVector<QSize> &sizes);
    QSize estimatedSize(qsizetype item) const;
    QRect logicalItemRect(qsizetype item) const;
    int itemColumn(qsizetype item) const;
    qsizetype nearestVertical(qsizetype item, bool down) const;
    qsizetype nearestHorizontal(qsizetype item, bool right) const;
    /// Finds the item whose center is closest to the source center plus a delta.
    qsizetype nearestByVerticalOffset(qsizetype item, qint64 delta) const;
    qsizetype nextItemInColumn(qsizetype item) const;
    /// Returns source item numbers intersecting the vertical interval.
    QVector<qsizetype> itemsInVerticalRange(qint64 top, qint64 bottom) const;
    /// Returns the first item in each column that contains or follows an offset.
    QVector<qsizetype> itemsAtOrAfterOffset(qint64 offset) const;

private:
    struct Geometry {
        QRect rect;
        int column = -1;
    };

    void rebuild();
    void updateColumns();
    int heightFor(qsizetype item, int width) const;
    QVector<qsizetype> columnItemsAt(int column, qint64 top, qint64 bottom) const;
    int clampColumn(int column) const;
    void rebuildFrom(qsizetype firstItem);

    qsizetype m_count = 0;
    int m_width = 0;
    int m_minimumWidth = 100;
    int m_maximumWidth = 200;
    int m_requestedColumns = 0;
    int m_columns = 1;
    int m_rowSpacing = 0;
    int m_columnSpacing = 0;
    int m_fixedHeight = 100;
    qreal m_aspectRatio = 1.0;
    int m_estimateHeight = 100;
    QMargins m_margins;
    HeightMode m_heightMode = HeightMode::Fixed;
    QVector<QSize> m_estimatedSizes;
    QVector<int> m_explicitHeights;
    QVector<Geometry> m_geometry;
    QVector<QVector<qsizetype>> m_columnItems;
    qint64 m_contentExtent = 0;
};

} // namespace viv
