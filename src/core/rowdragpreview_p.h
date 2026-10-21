#pragma once

#include <QHash>
#include <QVector>
#include <QtGlobal>

namespace viv {

// A visual insertion preview; destination uses Qt's insert-before convention.
// It keeps only sampled rows, never a mapping for the complete model.
class RowDragPreview
{
public:
    bool isActive() const { return m_source >= 0; }
    qsizetype source() const { return m_source; }
    qsizetype destination() const { return m_destination; }
    qreal extent() const { return m_extent; }

    // Retarget from the current interpolated positions, including quick reversals.
    void retarget(qsizetype source, qsizetype destination, qreal extent,
                  const QVector<qsizetype> &rows, qreal progress)
    {
        QHash<qsizetype, qreal> from;
        for (qsizetype row : rows)
            from.insert(row, offset(row, progress));
        m_source = source;
        m_destination = destination;
        m_extent = qMax<qreal>(0, extent);
        m_from.swap(from);
    }

    // Newly materialized rows start at their target; only existing rows tween.
    qreal offset(qsizetype row, qreal progress) const
    {
        if (!isActive() || row == m_source)
            return 0;
        const qreal target = targetOffset(row);
        const qreal from = m_from.value(row, target);
        return from + (target - from) * qBound<qreal>(0, progress, 1);
    }

    // Model structure is never modified by the preview.
    void clear()
    {
        m_source = m_destination = -1;
        m_extent = 0;
        m_from.clear();
    }

private:
    qreal targetOffset(qsizetype row) const
    {
        if (m_destination < m_source && row >= m_destination && row < m_source)
            return m_extent;
        if (m_destination > m_source + 1 && row > m_source && row < m_destination)
            return -m_extent;
        return 0;
    }

    qsizetype m_source = -1;
    qsizetype m_destination = -1;
    qreal m_extent = 0;
    QHash<qsizetype, qreal> m_from;
};

} // namespace viv
