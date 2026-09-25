#include <virtualitemviews/tablepane.h>

#include <virtualitemviews/headergeometry.h>

#include <QSet>

#include <algorithm>

namespace viv {

namespace {
/// Keeps the frozen sets deduplicated; the visual order is derived in update().
QVector<int> normalized(const QVector<int> &logicalColumns)
{
    QSet<int> seen;
    QVector<int> result;
    result.reserve(logicalColumns.size());
    for (int logical : logicalColumns) {
        if (logical < 0 || seen.contains(logical))
            continue;
        seen.insert(logical);
        result.append(logical);
    }
    return result;
}

/// One pane while the layout is being built.
struct ResolvedPane
{
    TablePane pane;
    /// 64-bit: a pane may hold very many columns, and the group extent feeds the
    /// (64-bit) scroll offset.
    qint64 extent = 0;
    int width = 0;
    int x = 0;
    /// Accumulated content x of the pane's own scroll space.
    qint64 contentX = 0;
    VisibleRange window;
};

/// How far outside the viewport a column x is still reported exactly. Anything
/// further away is "off screen" anyway, and keeping the value bounded keeps the
/// 32-bit QRect/QWidget arithmetic from overflowing on very wide tables.
constexpr qint64 kMaxOffscreenX = qint64(1) << 20;
} // namespace

void TablePaneLayout::setFrozenColumns(const QVector<int> &logicalColumns)
{
    const QVector<int> normalizedColumns = normalized(logicalColumns);
    if (normalizedColumns == m_frozenLeft)
        return;
    m_frozenLeft = normalizedColumns;
}

void TablePaneLayout::setFrozenRightColumns(const QVector<int> &logicalColumns)
{
    const QVector<int> normalizedColumns = normalized(logicalColumns);
    if (normalizedColumns == m_frozenRight)
        return;
    m_frozenRight = normalizedColumns;
}

void TablePaneLayout::setPaneSpecs(const QVector<TablePaneSpec> &specs)
{
    if (specs == m_specs)
        return;
    m_specs = specs;
    // An explicit list replaces the frozen sets: they are the shorthand for the
    // default three panes, not a second truth.
    if (!m_specs.isEmpty()) {
        m_frozenLeft.clear();
        m_frozenRight.clear();
    }
}

void TablePaneLayout::remapLogicalColumns(const std::function<int(int)> &remap)
{
    const auto remapList = [&remap](const QVector<int> &columns) {
        QVector<int> result;
        result.reserve(columns.size());
        for (int logical : columns) {
            const int mapped = remap(logical);
            if (mapped >= 0)
                result.append(mapped);
        }
        return result;
    };

    m_frozenLeft = remapList(m_frozenLeft);
    m_frozenRight = remapList(m_frozenRight);
    for (TablePaneSpec &spec : m_specs)
        spec.logicalColumns = remapList(spec.logicalColumns);
}

void TablePaneLayout::insertLogicalColumns(int first, int count)
{
    if (count <= 0)
        return;
    remapLogicalColumns([first, count](int logical) {
        return logical >= first ? logical + count : logical;
    });
}

void TablePaneLayout::removeLogicalColumns(int first, int count)
{
    if (count <= 0)
        return;
    remapLogicalColumns([first, count](int logical) {
        if (logical >= first + count)
            return logical - count;
        if (logical >= first)
            return -1;                       // this column is gone
        return logical;
    });
}

void TablePaneLayout::moveLogicalColumns(int start, int count, int destination)
{
    if (count <= 0 || start < 0 || destination < 0)
        return;
    // Same permutation as HeaderGeometry::moveLogicalSections(): the destination
    // is expressed in pre-move coordinates.
    const int target = destination > start + count - 1 ? destination - count : destination;
    if (target == start)
        return;
    remapLogicalColumns([start, count, target](int logical) {
        if (logical >= start && logical < start + count)
            return target + (logical - start);
        if (target < start) {
            if (logical >= target && logical < start)
                return logical + count;
        } else if (logical >= start + count && logical < target + count) {
            return logical - count;
        }
        return logical;
    });
}

qint64 TablePaneLayout::extentOf(const QVector<int> &logicalColumns) const
{
    if (!m_geometry)
        return 0;
    qint64 extent = 0;
    for (int logical : logicalColumns)
        extent += m_geometry->sectionSize(logical);
    return extent;
}

QVector<int> TablePaneLayout::visualOrderOf(const QVector<int> &logicalColumns) const
{
    QVector<int> result;
    if (!m_geometry)
        return result;
    QSet<int> wanted;
    for (int logical : logicalColumns) {
        if (m_geometry->isSectionHidden(logical))
            continue; // a hidden column is not part of any pane
        wanted.insert(logical);
    }
    if (wanted.isEmpty())
        return result;

    const int count = m_geometry->sectionCount();
    result.reserve(wanted.size());
    for (int visual = 0; visual < count && result.size() < wanted.size(); ++visual) {
        const int logical = m_geometry->logicalIndex(visual);
        if (logical >= 0 && wanted.contains(logical))
            result.append(logical);
    }
    return result;
}

bool TablePaneLayout::update(int viewportWidth, int viewportHeight)
{
    const int width = qMax(0, viewportWidth);
    const int height = qMax(0, viewportHeight);
    const int count = m_geometry ? m_geometry->sectionCount() : 0;

    const QVector<TablePane> previousPanes = m_panes;
    const QHash<int, qint64> previousExtents = m_groupExtents;
    const QHash<int, int> previousWidths = m_groupWidths;
    const int previousWidth = m_viewportWidth;
    const int previousHeight = m_viewportHeight;
    const VisibleRange previousVisibleScrollable = m_visibleScrollable;

    m_viewportWidth = width;
    m_viewportHeight = height;
    m_panes.clear();
    m_viewportXByLogical.fill(-1, count);
    m_paneByLogical.fill(int(TablePane::Type::Scrollable), count);
    m_paneIndexByLogical.fill(-1, count);
    m_groupByLogical.fill(-1, count);
    m_groupExtents.clear();
    m_groupWidths.clear();
    m_visibleScrollable = VisibleRange();
    m_primaryScrollGroup = -1;
    m_paneWindows.clear();

    if (!m_geometry || count <= 0) {
        return previousPanes != m_panes || previousExtents != m_groupExtents
            || previousWidths != m_groupWidths || previousWidth != m_viewportWidth
            || previousHeight != m_viewportHeight
            || previousVisibleScrollable.first != m_visibleScrollable.first
            || previousVisibleScrollable.last != m_visibleScrollable.last;
    }

    // -----------------------------------------------------------------------
    // 1. The ordered pane list: either the explicit specs or the default
    //    "frozen left | scrollable | frozen right" (frozen sets, §31).
    // -----------------------------------------------------------------------
    QVector<ResolvedPane> resolved;
    if (!m_specs.isEmpty()) {
        resolved.reserve(m_specs.size());
        for (const TablePaneSpec &spec : m_specs) {
            ResolvedPane pane;
            pane.pane.type = spec.isFrozen() ? TablePane::Type::FrozenLeft
                                             : TablePane::Type::Scrollable;
            pane.pane.scrollGroup = spec.isFrozen() ? -1 : spec.scrollGroup;
            pane.pane.logicalColumns = visualOrderOf(spec.logicalColumns);
            resolved.append(pane);
        }
    } else {
        const QVector<int> leftColumns = visualOrderOf(m_frozenLeft);
        QVector<int> rightColumns;
        for (int logical : visualOrderOf(m_frozenRight)) {
            if (!leftColumns.contains(logical))
                rightColumns.append(logical);
        }

        ResolvedPane left;
        left.pane.type = TablePane::Type::FrozenLeft;
        left.pane.logicalColumns = leftColumns;
        if (!left.pane.logicalColumns.isEmpty())
            resolved.append(left);

        ResolvedPane scrollable;
        scrollable.pane.type = TablePane::Type::Scrollable;
        scrollable.pane.scrollGroup = 0;
        for (int visual = 0; visual < count; ++visual) {
            const int logical = m_geometry->logicalIndex(visual);
            if (logical < 0 || m_geometry->isSectionHidden(logical))
                continue;
            if (leftColumns.contains(logical) || rightColumns.contains(logical))
                continue;
            scrollable.pane.logicalColumns.append(logical);
        }
        resolved.append(scrollable);

        ResolvedPane right;
        right.pane.type = TablePane::Type::FrozenRight;
        right.pane.logicalColumns = rightColumns;
        if (!right.pane.logicalColumns.isEmpty())
            resolved.append(right);
    }

    // A frozen pane that sits after the primary scrolling pane reads as
    // FrozenRight, so paneOfColumn()/isFrozenColumn() keep their meaning for an
    // explicit list as well.
    int primaryIndex = -1;
    for (int index = 0; index < resolved.size(); ++index) {
        if (resolved.at(index).pane.type == TablePane::Type::Scrollable) {
            primaryIndex = index;
            break;
        }
    }
    for (int index = 0; index < resolved.size(); ++index) {
        ResolvedPane &pane = resolved[index];
        pane.extent = extentOf(pane.pane.logicalColumns);
        if (pane.pane.type == TablePane::Type::Scrollable)
            continue;
        pane.pane.type = primaryIndex < 0 || index < primaryIndex ? TablePane::Type::FrozenLeft
                                                                 : TablePane::Type::FrozenRight;
    }
    // The primary group is the one that follows the committed header geometry: it
    // is the group of the first scrolling pane, which is group 0 for the default
    // panes and for any list that starts its scrolling pane at group 0.
    if (primaryIndex >= 0)
        m_primaryScrollGroup = resolved.at(primaryIndex).pane.scrollGroup;

    // -----------------------------------------------------------------------
    // 2. Widths: a frozen pane takes its own extent (in order, so panes that do
    //    not fit are compressed and the last ones collapse to 0) and the scrolling
    //    panes share what is left in proportion to their extents. With a single
    //    scrolling pane - the default three panes, and every list that leaves the
    //    scrolling to one pane - this is exactly "the primary pane takes what is
    //    left", pixel for pixel as before. Distributing instead of letting the
    //    first scrolling pane take everything is what makes a second scroll group
    //    usable at all: otherwise it either shows all of its columns or squeezes
    //    the primary pane down to 0.
    // -----------------------------------------------------------------------
    int remaining = width;
    for (int index = 0; index < resolved.size(); ++index) {
        ResolvedPane &pane = resolved[index];
        if (pane.pane.type == TablePane::Type::Scrollable)
            continue;
        pane.width = qMin(pane.extent, remaining);
        remaining -= pane.width;
    }
    remaining = qMax(0, remaining);
    qint64 scrollingExtent = 0;
    for (const ResolvedPane &pane : resolved) {
        if (pane.pane.type == TablePane::Type::Scrollable)
            scrollingExtent += pane.extent;
    }
    for (int index = 0; index < resolved.size(); ++index) {
        ResolvedPane &pane = resolved[index];
        if (pane.pane.type != TablePane::Type::Scrollable || index == primaryIndex)
            continue;
        if (scrollingExtent <= 0) {
            pane.width = 0;
            continue;
        }
        pane.width = int(qint64(remaining) * qint64(pane.extent) / scrollingExtent);
        remaining -= pane.width;
    }
    if (primaryIndex >= 0) {
        // The primary pane absorbs the rounding, so the widths always add up to
        // the viewport width - and with a single scrolling pane it takes exactly
        // what is left, like before.
        resolved[primaryIndex].width = qMax(0, remaining);
    }

    // Group extents and widths are known before any offset is clamped below.
    for (const ResolvedPane &pane : resolved) {
        if (pane.pane.type != TablePane::Type::Scrollable)
            continue;
        m_groupExtents[pane.pane.scrollGroup] += qint64(pane.extent);
        m_groupWidths[pane.pane.scrollGroup]
            = m_groupWidths.value(pane.pane.scrollGroup, 0) + pane.width;
    }

    // -----------------------------------------------------------------------
    // 3. Positions and column x. A pane lays out its own content: frozen panes
    //    never move, scrolling panes of one group share the group's offset.
    // -----------------------------------------------------------------------
    int x = 0;
    for (ResolvedPane &pane : resolved) {
        pane.x = x;
        pane.pane.viewportRect = QRect(x, 0, pane.width, height);
        x += pane.width;
    }

    for (int paneIndex = 0; paneIndex < resolved.size(); ++paneIndex) {
        ResolvedPane &pane = resolved[paneIndex];
        const bool frozen = pane.pane.type != TablePane::Type::Scrollable;
        const qint64 offset = frozen ? 0 : groupOffset(pane.pane.scrollGroup);
        qint64 contentX = 0;
        for (int logical : pane.pane.logicalColumns) {
            const int size = m_geometry->sectionSize(logical);
            // Clamp into a sane neighbourhood of the window: a pane may hold a
            // 64-bit extent (very wide tables), but a column x is only meaningful
            // near the viewport, and QRect/QWidget arithmetic is 32-bit (Qt even
            // asserts on overflow). Columns further away collapse to the sentinel.
            const qint64 localX = contentX - offset;
            const qint64 wanted = qint64(pane.x) + localX;
            const qint64 limit = qint64(width) + kMaxOffscreenX;
            const int viewportX = int(qBound(-kMaxOffscreenX, wanted, limit));
            m_viewportXByLogical[logical] = viewportX;
            m_paneByLogical[logical] = int(pane.pane.type);
            m_paneIndexByLogical[logical] = paneIndex;
            m_groupByLogical[logical] = frozen ? -1 : pane.pane.scrollGroup;

            if (size > 0 && viewportX < pane.x + pane.width && viewportX + size > pane.x) {
                if (pane.window.first < 0) {
                    pane.window.first = m_geometry->visualIndex(logical);
                    pane.window.last = pane.window.first;
                } else {
                    pane.window.last = m_geometry->visualIndex(logical);
                }
            }
            contentX += size;
        }
        pane.contentX = contentX;
        m_paneWindows.insert(paneIndex, pane.window);
        if (pane.pane.type == TablePane::Type::Scrollable && pane.window.isValid()) {
            // Every scrolling pane contributes (§43 "advanced panes"): with several
            // scroll groups the scrollable columns on screen are the union of their
            // windows, and one group may hold more than one pane.
            m_visibleScrollable = m_visibleScrollable.isValid()
                ? VisibleRange{qMin(m_visibleScrollable.first, pane.window.first),
                               qMax(m_visibleScrollable.last, pane.window.last)}
                : pane.window;
        }
    }

    for (const ResolvedPane &pane : resolved)
        m_panes.append(pane.pane);

    return previousPanes != m_panes || previousExtents != m_groupExtents
        || previousWidths != m_groupWidths || previousWidth != m_viewportWidth
        || previousHeight != m_viewportHeight
        || previousVisibleScrollable.first != m_visibleScrollable.first
        || previousVisibleScrollable.last != m_visibleScrollable.last;
}

TablePane TablePaneLayout::pane(TablePane::Type type) const
{
    for (const TablePane &candidate : m_panes) {
        if (candidate.type == type)
            return candidate;
    }
    if (type == TablePane::Type::Scrollable) {
        // The primary scrolling pane always exists, even when the other panes
        // eat the whole viewport.
        TablePane empty;
        empty.type = type;
        empty.scrollGroup = 0;
        empty.viewportRect = QRect(0, 0, 0, m_viewportHeight);
        return empty;
    }
    return TablePane();
}

TablePane TablePaneLayout::paneAt(int paneIndex) const
{
    if (paneIndex < 0 || paneIndex >= m_panes.size())
        return TablePane();
    return m_panes.at(paneIndex);
}

int TablePaneLayout::paneIndexOfColumn(int logicalIndex) const
{
    if (logicalIndex < 0 || logicalIndex >= m_paneIndexByLogical.size())
        return -1;
    return m_paneIndexByLogical.at(logicalIndex);
}

int TablePaneLayout::columnViewportX(int logicalIndex) const
{
    if (logicalIndex < 0 || logicalIndex >= m_viewportXByLogical.size())
        return -1;
    return m_viewportXByLogical.at(logicalIndex);
}

TablePane::Type TablePaneLayout::paneOfColumn(int logicalIndex) const
{
    if (logicalIndex < 0 || logicalIndex >= m_paneByLogical.size())
        return TablePane::Type::Scrollable;
    return TablePane::Type(m_paneByLogical.at(logicalIndex));
}

int TablePaneLayout::scrollGroupOfColumn(int logicalIndex) const
{
    if (logicalIndex < 0 || logicalIndex >= m_groupByLogical.size())
        return -1;
    return m_groupByLogical.at(logicalIndex);
}

QVector<int> TablePaneLayout::scrollGroups() const
{
    QVector<int> groups;
    for (const TablePane &candidate : m_panes) {
        if (candidate.type != TablePane::Type::Scrollable || candidate.scrollGroup < 0)
            continue;
        if (!groups.contains(candidate.scrollGroup))
            groups.append(candidate.scrollGroup);
    }
    std::sort(groups.begin(), groups.end());
    return groups;
}

int TablePaneLayout::primaryPaneIndex() const
{
    for (int index = 0; index < m_panes.size(); ++index) {
        if (m_panes.at(index).type == TablePane::Type::Scrollable)
            return index;
    }
    return -1;
}

qint64 TablePaneLayout::groupOffset(int scrollGroup) const
{
    qint64 offset = 0;
    if (scrollGroup >= 0 && scrollGroup == m_primaryScrollGroup) {
        // The primary group follows the committed header geometry: the scroll bar
        // and every geometry query keep working exactly as before.
        offset = m_geometry ? m_geometry->viewportOffset() : 0;
    } else if (scrollGroup >= 0) {
        offset = m_groupOffsets.value(scrollGroup, 0);
    }
    return qBound<qint64>(qint64(0), offset, maximumGroupOffset(scrollGroup));
}

void TablePaneLayout::setGroupOffset(int scrollGroup, qint64 offset)
{
    if (scrollGroup < 0 || scrollGroup == m_primaryScrollGroup)
        return; // the application scrolls the primary group through the view
    m_groupOffsets.insert(scrollGroup, qMax<qint64>(0, offset));
}

QVector<int> TablePaneLayout::columnsForLayout(int overscan) const
{
    QVector<int> columns;
    if (!m_geometry)
        return columns;
    const int count = m_geometry->sectionCount();
    if (count <= 0)
        return columns;

    // Every pane contributes: a frozen pane always, a scrolling pane inside its
    // own window (widened by \a overscan sections).
    QVector<VisibleRange> windows;
    windows.reserve(m_panes.size());
    for (int paneIndex = 0; paneIndex < m_panes.size(); ++paneIndex) {
        const VisibleRange window = m_paneWindows.value(paneIndex, VisibleRange());
        windows.append(window.isValid()
                           ? VisibleRange::expanded(window.first, window.last, qMax(0, overscan),
                                                    qMax(0, overscan), count)
                           : window);
    }

    columns.reserve(count);
    for (int visual = 0; visual < count; ++visual) {
        const int logical = m_geometry->logicalIndex(visual);
        if (logical < 0 || m_geometry->isSectionHidden(logical))
            continue;
        const int paneIndex = paneIndexOfColumn(logical);
        if (paneIndex < 0)
            continue;
        if (m_panes.at(paneIndex).type != TablePane::Type::Scrollable
            || windows.at(paneIndex).contains(visual)) {
            columns.append(logical);
        }
    }
    return columns;
}

} // namespace viv
