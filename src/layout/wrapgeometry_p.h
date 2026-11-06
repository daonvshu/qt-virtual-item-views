#pragma once

#include <QMargins>
#include <QRect>
#include <QVector>
#include <algorithm>
#include <limits>

namespace viv { namespace detail {

// Shared, widget-independent packing. All vertical arithmetic stays in 64 bits.
struct WrapSettings
{
    int height = 32;
    int itemSpacing = 8;
    int rowSpacing = 8;
    int minimumWidth = 1;
    int maximumWidth = 16777215;
    QMargins margins;
    Qt::Alignment alignment = Qt::AlignLeft;
    Qt::LayoutDirection direction = Qt::LeftToRight;
};

struct WrapRow { int first = 0; int count = 0; int usedWidth = 0; };
struct WrapCell { int x = 0; int width = 0; int row = 0; };

inline int wrapCoordinate(qint64 value)
{
    return int(qBound<qint64>(qint64(std::numeric_limits<int>::min()), value,
                             qint64(std::numeric_limits<int>::max())));
}

class WrapGeometry
{
public:
    QVector<WrapCell> cells;
    QVector<WrapRow> rows;
    int width = 0;
    WrapSettings settings;

    // Restart at the preceding display row: a smaller/removed item can join it.
    void rebuild(const QVector<int> &widths, int firstChanged = 0)
    {
        int firstRow = 0;
        if (firstChanged > 0 && !cells.isEmpty())
            firstRow = qMax(0, cells[qMin(firstChanged - 1, int(cells.size()) - 1)].row - 1);
        int first = firstRow < rows.size() ? rows[firstRow].first : 0;
        if (first > widths.size()) { first = 0; firstRow = 0; }
        rows.resize(firstRow);
        cells.resize(widths.size());
        const int available = qMax(1, wrapCoordinate(qint64(width)
            - settings.margins.left() - settings.margins.right()));
        while (first < widths.size()) {
            WrapRow row;
            row.first = first;
            while (first + row.count < widths.size()) {
                const int w = qMin(available, qBound(settings.minimumWidth,
                    widths[first + row.count], settings.maximumWidth));
                const qint64 next = qint64(row.usedWidth) + (row.count ? settings.itemSpacing : 0) + w;
                if (row.count && next > available)
                    break;
                row.usedWidth = int(next);
                ++row.count;
            }
            const int spare = available - row.usedWidth;
            int shift = 0;
            const bool rtl = settings.direction == Qt::RightToLeft;
            if (settings.alignment & Qt::AlignHCenter)
                shift = spare / 2;
            else if ((settings.alignment & Qt::AlignRight) != 0)
                shift = spare;
            // Left/Right are logical leading/trailing unless AlignAbsolute is set.
            if (rtl && !(settings.alignment & Qt::AlignAbsolute)
                && !(settings.alignment & Qt::AlignHCenter))
                shift = spare - shift;
            int used = 0;
            for (int i = first; i < first + row.count; ++i) {
                WrapCell &cell = cells[i];
                cell.width = qMin(available, qBound(settings.minimumWidth, widths[i], settings.maximumWidth));
                cell.x = settings.margins.left() + shift
                    + (rtl ? row.usedWidth - used - cell.width : used);
                cell.row = int(rows.size());
                used += cell.width;
                if (i + 1 < first + row.count)
                    used += settings.itemSpacing;
            }
            rows.append(row);
            first += row.count;
        }
    }

    qint64 stride() const { return qint64(settings.height) + settings.rowSpacing; }
    qint64 offset(int row) const { return settings.margins.top() + qint64(row) * stride(); }
    qint64 extent() const
    {
        return qint64(settings.margins.top()) + settings.margins.bottom()
            + (rows.isEmpty() ? 0 : qint64(rows.size()) * stride() - settings.rowSpacing);
    }
    int rowAt(qint64 y) const
    {
        if (rows.isEmpty()) return 0;
        if (y >= offset(int(rows.size()) - 1) + settings.height) return int(rows.size());
        return int(qBound<qint64>(qint64(0), (y - settings.margins.top()) / stride(), qint64(rows.size())));
    }
    QRect rect(int item, qint64 scroll = 0) const
    {
        if (item < 0 || item >= cells.size()) return {};
        const WrapCell &cell = cells[item];
        // Keep distant pinned widgets inside QWidget's safe coordinate range.
        const qint64 top = qBound<qint64>(-(qint64(1) << 24), offset(cell.row) - scroll, qint64(1) << 24);
        return QRect(cell.x, int(top), cell.width, settings.height);
    }
    int hit(const QPoint &point, qint64 scroll) const
    {
        const qint64 y = qint64(point.y()) + scroll;
        const int row = rowAt(y);
        if (row >= rows.size() || y < offset(row) || y >= offset(row) + settings.height)
            return -1;
        const WrapRow &r = rows[row];
        // Search in model order, reversing the horizontal coordinate for RTL.
        const bool rtl = settings.direction == Qt::RightToLeft;
        int low = r.first, high = r.first + r.count;
        while (low < high) {
            const int mid = low + (high - low) / 2;
            const WrapCell &c = cells[mid];
            if (rtl ? point.x() < c.x : point.x() >= c.x + c.width)
                low = mid + 1;
            else
                high = mid;
        }
        return low < r.first + r.count && point.x() >= cells[low].x
            && point.x() < cells[low].x + cells[low].width ? low : -1;
    }
};

} } // namespace viv::detail
