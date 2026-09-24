#include <virtualitemviews/tablepane.h>

#include <virtualitemviews/headergeometry.h>

#include <QSet>

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

int TablePaneLayout::extentOf(const QVector<int> &logicalColumns) const
{
    if (!m_geometry)
        return 0;
    int extent = 0;
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
    const int previousWidth = m_viewportWidth;
    const int previousHeight = m_viewportHeight;
    const VisibleRange previousVisibleScrollable = m_visibleScrollable;

    m_viewportWidth = width;
    m_viewportHeight = height;
    m_panes.clear();
    m_viewportXByLogical.fill(-1, count);
    m_paneByLogical.fill(int(TablePane::Type::Scrollable), count);
    m_scrollableExtent = 0;
    m_visibleScrollable = VisibleRange();

    if (!m_geometry || count <= 0) {
        return previousPanes != m_panes || previousWidth != m_viewportWidth
            || previousHeight != m_viewportHeight
            || previousVisibleScrollable.first != m_visibleScrollable.first
            || previousVisibleScrollable.last != m_visibleScrollable.last;
    }

    const QVector<int> leftColumns = visualOrderOf(m_frozenLeft);
    // A column frozen on both sides stays on the left, so it is not part of the
    // right pane (otherwise it would be painted and counted twice).
    QVector<int> rightColumns;
    for (int logical : visualOrderOf(m_frozenRight)) {
        if (!leftColumns.contains(logical))
            rightColumns.append(logical);
    }

    // Pane membership first: the scrollable loop below skips every frozen
    // column, and a column in both sets stays on the left.
    for (int logical : leftColumns) {
        m_paneByLogical[logical] = int(TablePane::Type::FrozenLeft);
        m_viewportXByLogical[logical] = 0; // positioned below
    }
    for (int logical : rightColumns) {
        if (m_paneByLogical.at(logical) != int(TablePane::Type::Scrollable))
            continue;
        m_paneByLogical[logical] = int(TablePane::Type::FrozenRight);
        m_viewportXByLogical[logical] = 0; // positioned below
    }

    const int leftExtent = qMin(extentOf(leftColumns), width);
    const int rightExtent = qMin(extentOf(rightColumns), qMax(0, width - leftExtent));
    const int scrollableX = leftExtent;
    const int scrollableWidth = qMax(0, width - leftExtent - rightExtent);

    TablePane leftPane;
    leftPane.type = TablePane::Type::FrozenLeft;
    leftPane.viewportRect = QRect(0, 0, leftExtent, height);
    leftPane.logicalColumns = leftColumns;

    TablePane scrollablePane;
    scrollablePane.type = TablePane::Type::Scrollable;
    scrollablePane.viewportRect = QRect(scrollableX, 0, scrollableWidth, height);

    TablePane rightPane;
    rightPane.type = TablePane::Type::FrozenRight;
    rightPane.viewportRect = QRect(width - rightExtent, 0, rightExtent, height);
    rightPane.logicalColumns = rightColumns;

    // Frozen left: laid out from the left edge, never scrolled.
    int x = 0;
    for (int logical : leftColumns) {
        m_viewportXByLogical[logical] = x;
        x += m_geometry->sectionSize(logical);
    }

    // Scrollable: content positions of the scrollable columns only, shifted by
    // the horizontal offset and moved to the right of the frozen left pane.
    const qint64 offset = m_geometry->viewportOffset();
    qint64 contentX = 0;
    for (int visual = 0; visual < count; ++visual) {
        const int logical = m_geometry->logicalIndex(visual);
        if (logical < 0 || m_geometry->isSectionHidden(logical))
            continue;
        if (m_paneByLogical.value(logical) != int(TablePane::Type::Scrollable))
            continue;
        const int size = m_geometry->sectionSize(logical);
        const int viewportX = scrollableX + int(contentX - offset);
        m_viewportXByLogical[logical] = viewportX;
        scrollablePane.logicalColumns.append(logical);
        // Visible part of the scrollable pane: the horizontal window is the
        // scrollable columns only, so frozen columns never shift it (§31).
        if (size > 0 && viewportX < scrollableX + scrollableWidth && viewportX + size > scrollableX) {
            if (m_visibleScrollable.first < 0) {
                m_visibleScrollable.first = visual;
                m_visibleScrollable.last = visual;
            } else {
                m_visibleScrollable.last = visual;
            }
        }
        contentX += size;
        m_scrollableExtent = contentX;
    }

    // Frozen right: laid out from the right edge, never scrolled.
    x = width - rightExtent;
    for (int logical : rightColumns) {
        if (m_paneByLogical.at(logical) != int(TablePane::Type::FrozenRight))
            continue; // a hidden column, or one that stayed on the left
        m_viewportXByLogical[logical] = x;
        x += m_geometry->sectionSize(logical);
    }

    if (!leftPane.isEmpty())
        m_panes.append(leftPane);
    m_panes.append(scrollablePane);
    if (!rightPane.isEmpty())
        m_panes.append(rightPane);

    return previousPanes != m_panes || previousWidth != m_viewportWidth
        || previousHeight != m_viewportHeight
        || previousVisibleScrollable.first != m_visibleScrollable.first
        || previousVisibleScrollable.last != m_visibleScrollable.last;
}

TablePane TablePaneLayout::pane(TablePane::Type type) const
{
    for (const TablePane &pane : m_panes) {
        if (pane.type == type)
            return pane;
    }
    if (type == TablePane::Type::Scrollable) {
        // The scrollable pane always exists, even when the frozen panes eat the
        // whole viewport.
        TablePane empty;
        empty.type = type;
        empty.viewportRect = QRect(0, 0, 0, m_viewportHeight);
        return empty;
    }
    return TablePane();
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

QVector<int> TablePaneLayout::columnsForLayout(int overscan) const
{
    QVector<int> columns;
    if (!m_geometry)
        return columns;
    const int count = m_geometry->sectionCount();
    if (count <= 0)
        return columns;

    const VisibleRange window = VisibleRange::expanded(
        m_visibleScrollable.first, m_visibleScrollable.last, qMax(0, overscan), qMax(0, overscan),
        count);
    columns.reserve(int(window.count()) + m_frozenLeft.size() + m_frozenRight.size());
    for (int visual = 0; visual < count; ++visual) {
        const int logical = m_geometry->logicalIndex(visual);
        if (logical < 0 || m_geometry->isSectionHidden(logical))
            continue;
        // Frozen columns are always laid out, the scrollable ones only inside
        // the visible window (widened by the overscan).
        if (paneOfColumn(logical) != TablePane::Type::Scrollable || window.contains(visual))
            columns.append(logical);
    }
    return columns;
}

} // namespace viv
