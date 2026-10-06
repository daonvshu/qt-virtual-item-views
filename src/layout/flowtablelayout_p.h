#pragma once

#include <virtualitemviews/layoutpolicy.h>
#include <QMargins>

namespace viv {

// Arithmetic grid layout; storage and resize cost do not depend on item count.
class VIRTUALITEMVIEWS_EXPORT FlowTableLayout : public LayoutPolicy
{
public:
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
    bool isVariableSized() const override { return m_proportional; }
    bool needsInitialMeasurement() const override { return false; }
    void resetItems(qsizetype count, int estimate) override;
    void insertItems(qsizetype index, qsizetype count, int estimate) override;
    void removeItems(qsizetype index, qsizetype count) override;
    void moveItems(qsizetype from, qsizetype count, qsizetype to) override;
    void setItemSize(qsizetype item, int size) override;

    // Width limits are preferred; exact filling takes priority at infeasible sizes.
    void setWidthRange(int minimum, int maximum);
    int minimumWidth() const { return m_minimum; }
    int maximumWidth() const { return m_maximum; }
    void setFixedHeight(int height);
    int fixedHeight() const { return m_fixedHeight; }
    void setProportional(bool enabled);
    bool isProportional() const { return m_proportional; }
    void setAspectRatio(qreal widthOverHeight);
    qreal aspectRatio() const { return m_ratio; }
    void setSpacing(int rows, int columns);
    int rowSpacing() const { return m_rowSpacing; }
    int columnSpacing() const { return m_columnSpacing; }
    void setMargins(const QMargins &margins);
    QMargins margins() const { return m_margins; }
    int columnCount() const { return m_columns; }
    qsizetype rowCount() const;
    int columnWidth(int column) const;
    int columnX(int column) const;
    int rowHeight() const;

private:
    void updateColumns();
    int heightForWidth(int width) const;
    qint64 stride() const;
    qsizetype m_count = 0;
    int m_width = 0;
    int m_minimum = 100;
    int m_maximum = 200;
    int m_fixedHeight = 100;
    int m_rowSpacing = 0;
    int m_columnSpacing = 0;
    int m_columns = 1;
    int m_cellSpace = 0;
    QMargins m_margins;
    bool m_proportional = false;
    qreal m_ratio = 1.0;
};

} // namespace viv
