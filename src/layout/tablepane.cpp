#include <virtualitemviews/tablepane.h>

#include <virtualitemviews/headergeometry.h>

#include <QSet>

#include <algorithm>

namespace viv {

namespace {
/// Result of normalizing a pane list: what is stored, plus the diagnostics that
/// describe what had to be fixed. They are reported by the caller only when the
/// list really changes, so passing the same broken list twice stays a no-op.
struct ValidatedPaneSpecs
{
    QVector<TablePaneSpec> specs;
    /// At most one message per kind of problem, in the order they were found.
    QVector<QString> warnings;
};

/// A column may be claimed by one pane only, a scroll group is never negative and
/// the panes of one group have to be neighbours (docs/spans.md §5). A broken
/// configuration would otherwise show one column twice in the header while the body
/// ownership falls back to "whoever was written last".
ValidatedPaneSpecs validated(const QVector<TablePaneSpec> &specs)
{
    ValidatedPaneSpecs result;
    result.specs.reserve(specs.size());
    QSet<int> claimed;
    QHash<int, int> lastPaneOfGroup;
    bool reportedDuplicate = false;
    bool reportedNegativeGroup = false;
    bool reportedSplitGroup = false;
    for (int index = 0; index < specs.size(); ++index) {
        const TablePaneSpec &spec = specs.at(index);
        TablePaneSpec pane = spec;
        pane.logicalColumns.clear();
        for (int logical : spec.logicalColumns) {
            if (logical < 0 || claimed.contains(logical)) {
                if (!reportedDuplicate) {
                    result.warnings.append(
                        QStringLiteral("TablePaneLayout::setPaneSpecs(): pane %1 shows column %2, "
                                       "which is negative or already shown by an earlier pane; "
                                       "the earlier pane keeps it")
                            .arg(index)
                            .arg(logical));
                    reportedDuplicate = true;
                }
                continue;
            }
            claimed.insert(logical);
            pane.logicalColumns.append(logical);
        }
        if (pane.isFrozen()) {
            result.specs.append(pane);
            continue;
        }
        if (pane.scrollGroup < 0) {
            if (!reportedNegativeGroup) {
                result.warnings.append(
                    QStringLiteral("TablePaneLayout::setPaneSpecs(): scroll group %1 is negative; "
                                   "using group 0")
                        .arg(pane.scrollGroup));
                reportedNegativeGroup = true;
            }
            pane.scrollGroup = 0;
        }
        // Looking the group up *before* inserting the new pane tells "the pane right
        // before this one is of the same group" apart from "some earlier pane was":
        // only the latter splits one group into two places.
        const auto previous = lastPaneOfGroup.constFind(pane.scrollGroup);
        if (previous != lastPaneOfGroup.constEnd() && previous.value() != index - 1) {
            if (!reportedSplitGroup) {
                result.warnings.append(
                    QStringLiteral("TablePaneLayout::setPaneSpecs(): pane %1 repeats scroll group "
                                   "%2 after pane %3, so the group is not a run of neighbouring "
                                   "panes; it shares one offset and would scroll in two places")
                        .arg(index)
                        .arg(pane.scrollGroup)
                        .arg(previous.value()));
                reportedSplitGroup = true;
            }
        }
        lastPaneOfGroup.insert(pane.scrollGroup, index);
        result.specs.append(pane);
    }
    return result;
}

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
    const ValidatedPaneSpecs validatedSpecs = validated(specs);
    if (validatedSpecs.specs == m_specs)
        return;
    for (const QString &warning : validatedSpecs.warnings)
        qWarning("%s", qUtf8Printable(warning));
    m_specs = validatedSpecs.specs;
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
    m_panePrefixX.clear();
    m_paneSlotByLogical.fill(-1, count);
    m_paneSlotWindows.clear();
    m_paneByLogical.fill(int(TablePane::Type::Scrollable), count);
    m_paneIndexByLogical.fill(-1, count);
    m_groupByLogical.fill(-1, count);
    m_groupExtents.clear();
    m_groupWidths.clear();
    m_visibleScrollable = VisibleRange();
    m_columnVisits = 0;
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
        // extent is qint64 (§P1-5), the pane can never be wider than the viewport,
        // so the min is an int: spell the comparison out (Qt 5 has no mixed overload).
        pane.width = int(qMin<qint64>(pane.extent, remaining));
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

    // Pane-local prefix sums per pane: the column x and the visible window both
    // come from these, so a scroll never has to walk the columns again.
    m_panePrefixX.resize(resolved.size());
    m_paneSlotWindows.resize(resolved.size());
    for (int paneIndex = 0; paneIndex < resolved.size(); ++paneIndex) {
        ResolvedPane &pane = resolved[paneIndex];
        const bool frozen = pane.pane.type != TablePane::Type::Scrollable;
        QVector<qint64> &prefix = m_panePrefixX[paneIndex];
        prefix.clear();
        prefix.reserve(pane.pane.logicalColumns.size() + 1);
        qint64 contentX = 0;
        prefix.append(0);
        for (int slot = 0; slot < pane.pane.logicalColumns.size(); ++slot) {
            const int logical = pane.pane.logicalColumns.at(slot);
            m_paneByLogical[logical] = int(pane.pane.type);
            m_paneIndexByLogical[logical] = paneIndex;
            m_groupByLogical[logical] = frozen ? -1 : pane.pane.scrollGroup;
            m_paneSlotByLogical[logical] = slot;
            contentX += m_geometry->sectionSize(logical);
            prefix.append(contentX);
            ++m_columnVisits;
        }
        pane.contentX = contentX;
    }

    for (const ResolvedPane &pane : resolved)
        m_panes.append(pane.pane);
    refreshScrollWindowsImpl();

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
    if (logicalIndex < 0 || logicalIndex >= m_paneSlotByLogical.size())
        return -1;
    const int paneIndex = m_paneIndexByLogical.at(logicalIndex);
    const int slot = m_paneSlotByLogical.at(logicalIndex);
    if (paneIndex < 0 || slot < 0 || paneIndex >= m_panes.size())
        return -1;
    const TablePane &pane = m_panes.at(paneIndex);
    if (slot + 1 >= m_panePrefixX.value(paneIndex).size())
        return -1;

    const bool frozen = pane.type != TablePane::Type::Scrollable;
    const qint64 offset = frozen ? 0 : groupOffset(pane.scrollGroup);
    // Clamp into a sane neighbourhood of the window: a pane may hold a 64-bit
    // extent (very wide tables), but a column x is only meaningful near the
    // viewport, and QRect/QWidget arithmetic is 32-bit (Qt even asserts on
    // overflow). Columns further away collapse to the sentinel.
    const qint64 wanted = qint64(pane.viewportRect.x())
        + m_panePrefixX.at(paneIndex).at(slot) - offset;
    const int limit = pane.viewportRect.width() + int(kMaxOffscreenX);
    return int(qBound(-kMaxOffscreenX, wanted, qint64(limit)));
}

bool TablePaneLayout::refreshScrollWindows()
{
    m_columnVisits = 0;
    return refreshScrollWindowsImpl();
}

bool TablePaneLayout::refreshScrollWindowsImpl()
{
    const VisibleRange previousScrollable = m_visibleScrollable;
    m_paneWindows.clear();
    m_visibleScrollable = VisibleRange();
    m_paneSlotWindows.resize(m_panes.size());

    for (int paneIndex = 0; paneIndex < m_panes.size(); ++paneIndex) {
        const TablePane &pane = m_panes.at(paneIndex);
        m_paneSlotWindows[paneIndex] = {-1, -1};
        const int slotCount = pane.logicalColumns.size();
        const int paneWidth = pane.viewportRect.width();
        if (slotCount <= 0 || paneWidth <= 0 || paneIndex >= m_panePrefixX.size())
            continue;

        const bool frozen = pane.type != TablePane::Type::Scrollable;
        const qint64 offset = frozen ? 0 : groupOffset(pane.scrollGroup);
        const QVector<qint64> &prefix = m_panePrefixX.at(paneIndex);
        if (prefix.size() != slotCount + 1)
            continue;                       // stale cache: the next update() rebuilds

        // Binary search instead of walking the pane's columns: the first column
        // that ends after the pane's left edge, and the last one that starts before
        // its right edge.
        const qint64 left = offset;
        const qint64 right = offset + paneWidth;
        int low = 0;
        int high = slotCount;
        while (low < high) {
            const int mid = (low + high) / 2;
            if (prefix.at(mid + 1) > left)
                high = mid;
            else
                low = mid + 1;
        }
        const int firstSlot = low;
        low = firstSlot;
        high = slotCount;
        while (low < high) {
            const int mid = (low + high) / 2;
            if (prefix.at(mid) < right)
                low = mid + 1;
            else
                high = mid;
        }
        const int lastSlot = low - 1;
        if (firstSlot >= slotCount || lastSlot < firstSlot) {
            ++m_columnVisits;               // the (log) search itself
            continue;
        }
        m_paneSlotWindows[paneIndex] = {firstSlot, lastSlot};
        m_columnVisits += 2;                // two binary searches, not `slotCount` visits

        const VisibleRange window{m_geometry->visualIndex(pane.logicalColumns.at(firstSlot)),
                                  m_geometry->visualIndex(pane.logicalColumns.at(lastSlot))};
        m_paneWindows.insert(paneIndex, window);
        if (pane.type != TablePane::Type::Scrollable)
            continue;
        // Every scrolling pane contributes (§43 "advanced panes"): with several
        // scroll groups the scrollable columns on screen are the union of their
        // windows, and one group may hold more than one pane.
        m_visibleScrollable = m_visibleScrollable.isValid()
            ? VisibleRange{qMin(m_visibleScrollable.first, window.first),
                           qMax(m_visibleScrollable.last, window.last)}
            : window;
    }

    return previousScrollable.first != m_visibleScrollable.first
        || previousScrollable.last != m_visibleScrollable.last;
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
    if (m_geometry->sectionCount() <= 0)
        return columns;

    // Every pane contributes, and only its own window: a frozen pane always, a
    // scrolling pane inside its window widened by \a overscan columns. Walking the
    // panes keeps this proportional to the window instead of the column count.
    const int extra = qMax(0, overscan);
    for (int paneIndex = 0; paneIndex < m_panes.size(); ++paneIndex) {
        const TablePane &pane = m_panes.at(paneIndex);
        const int slotCount = pane.logicalColumns.size();
        if (slotCount <= 0)
            continue;
        if (pane.type != TablePane::Type::Scrollable) {
            for (int slot = 0; slot < slotCount; ++slot)
                columns.append(pane.logicalColumns.at(slot));
            continue;
        }
        const QPair<int, int> window = m_paneSlotWindows.value(paneIndex, {-1, -1});
        if (window.first < 0)
            continue;
        const int first = qMax(0, window.first - extra);
        const int last = qMin(slotCount - 1, window.second + extra);
        for (int slot = first; slot <= last; ++slot)
            columns.append(pane.logicalColumns.at(slot));
    }
    return columns;
}

} // namespace viv
