#pragma once

#include <virtualitemviews/layoutpolicy.h>
#include "wrapgeometry_p.h"

namespace viv {

// Model-row identities stay independent of display-row packing.
class VIRTUALITEMVIEWS_EXPORT WrapItemLayout : public LayoutPolicy
{
public:
    Qt::Orientation orientation() const override { return Qt::Vertical; }
    qsizetype itemCount() const override { return m_widths.size(); }
    int itemSize(qsizetype item) const override;
    qint64 offsetOf(qsizetype item) const override;
    qsizetype indexAtOffset(qint64 offset) const override;
    qsizetype lastIndexAtOffset(qint64 offset) const override;
    qint64 contentExtent() const override { return geometry().extent(); }
    int crossExtent() const override { return m_geometry.width; }
    void setCrossExtent(int extent) override;
    QRect itemRect(qsizetype item, qint64 offset) const override;
    qsizetype itemAtPoint(const QPoint &point, qint64 offset) const override;
    bool isVariableSized() const override { return false; }
    bool needsInitialMeasurement() const override { return false; }
    void resetItems(qsizetype count, int estimate) override;
    void insertItems(qsizetype index, qsizetype count, int estimate) override;
    void removeItems(qsizetype index, qsizetype count) override;
    void moveItems(qsizetype from, qsizetype count, qsizetype to) override;
    void setItemSize(qsizetype, int) override {}
    void setWidths(int first, const QVector<int> &widths);
    void setSettings(const detail::WrapSettings &settings);
    const detail::WrapSettings &settings() const { return m_geometry.settings; }
    const detail::WrapGeometry &geometry() const;
    qint64 extentForWidth(int width) const;

private:
    void invalidate(int first = 0);
    QVector<int> m_widths;
    mutable detail::WrapGeometry m_geometry;
    mutable int m_dirty = 0;
    mutable int m_hintWidth = -1;
    mutable qint64 m_hintExtent = 0;
};

} // namespace viv
