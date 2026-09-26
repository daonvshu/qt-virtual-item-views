#include <virtualitemviews/headergeometry.h>

#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <limits>

namespace viv {

// A shared build has to export these public constants (see virtualitemview.cpp).
namespace {
[[maybe_unused]] const void *const kExportedConstants[] = {
    &HeaderGeometry::kStateMagic,
    &HeaderGeometry::kStateVersion,
};
} // namespace

namespace {
/// How far outside the viewport a section x is still reported exactly. Anything
/// further away is off screen anyway, and keeping the value bounded keeps the
/// 32-bit QRect/QWidget arithmetic from overflowing on very wide tables.
constexpr qint64 kMaxOffscreenX = qint64(1) << 20;
} // namespace

HeaderGeometry::HeaderGeometry(Qt::Orientation orientation, QObject *parent)
    : QObject(parent)
    , m_orientation(orientation)
{
}

HeaderGeometry::~HeaderGeometry() = default;

// ---------------------------------------------------------------------------
// Section set
// ---------------------------------------------------------------------------

void HeaderGeometry::setSectionCount(int count)
{
    const int clamped = qMax(0, count);
    if (clamped == sectionCount())
        return;

    if (clamped < sectionCount()) {
        const int removedFrom = clamped;
        m_sections.resize(clamped);
        // Drop the removed logical indices from the visual order.
        for (int visual = m_visualToLogical.size() - 1; visual >= 0; --visual) {
            if (m_visualToLogical.at(visual) >= removedFrom)
                m_visualToLogical.remove(visual);
        }
        // A sort indicator that pointed into the removed tail has nothing left to name.
        // setSectionCount() is the path a model reset / setModel takes, so it has to do the
        // same cleanup as removeLogicalSections() (P1-7 of the second review).
        if (m_sortIndicatorSection >= clamped) {
            m_sortIndicatorSection = -1;
            emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
        }
    } else {
        for (int logical = sectionCount(); logical < clamped; ++logical) {
            Section section;
            section.size = m_defaultSectionSize;
            m_sections.append(section);
            m_visualToLogical.append(logical);
        }
    }

    ++m_orderRevision;                  // the visible order changed either way
    rebuildIndexMaps();

    invalidateCaches();
    emit sectionCountChanged(clamped);
    emitGeometryChanged();
}

void HeaderGeometry::rebuildIndexMaps()
{
    m_logicalToVisual.resize(sectionCount());
    for (int visual = 0; visual < m_visualToLogical.size(); ++visual)
        m_logicalToVisual[m_visualToLogical.at(visual)] = visual;
}

void HeaderGeometry::insertLogicalSections(int first, int count)
{
    if (count <= 0)
        return;
    const int total = sectionCount();
    const int at = qBound(0, first, total);

    QVector<Section> sections;
    sections.reserve(total + count);
    for (int logical = 0; logical < at; ++logical)
        sections.append(m_sections.at(logical));
    for (int index = 0; index < count; ++index) {
        Section section;
        section.size = m_defaultSectionSize;
        sections.append(section);
    }
    for (int logical = at; logical < total; ++logical)
        sections.append(m_sections.at(logical));
    m_sections = sections;

    // The state of every existing section stays with its item: only the logical
    // numbers after the insertion point shift.
    //
    // The new sections take the visual slots of the section that used to sit at the
    // insertion point (the "successor"), which is what QHeaderView does as well: an
    // untouched header whose visual order equals the logical one shows "A | X | B | C"
    // after X is inserted before B. Appending them at the end - what this used to do -
    // showed "A | B | C | X", i.e. a middle insert behaved like an append (P1-4 of the
    // second review). A custom visual order keeps its shape: the new sections land where
    // the successor column is, the rest keeps its relative order.
    int slot = m_visualToLogical.size();
    for (int visual = 0; visual < m_visualToLogical.size(); ++visual) {
        if (m_visualToLogical.at(visual) == at) {
            slot = visual;
            break;
        }
    }
    for (int visual = 0; visual < m_visualToLogical.size(); ++visual) {
        if (m_visualToLogical.at(visual) >= at)
            m_visualToLogical[visual] += count;
    }
    for (int index = 0; index < count; ++index)
        m_visualToLogical.insert(slot + index, at + index);
    ++m_orderRevision;
    rebuildIndexMaps();

    if (m_sortIndicatorSection >= at) {
        m_sortIndicatorSection += count;
        // The indicator names a column, so its *number* moved with the insert: listeners
        // that only watch sortIndicatorChanged have to be told (P1-6 of the second review).
        emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
    }

    invalidateCaches();
    emit sectionCountChanged(sectionCount());
    emitGeometryChanged();
}

void HeaderGeometry::removeLogicalSections(int first, int count)
{
    if (count <= 0)
        return;
    const int total = sectionCount();
    const int at = qBound(0, first, total);
    const int removed = qMin(count, total - at);
    if (removed <= 0)
        return;

    QVector<Section> sections;
    sections.reserve(total - removed);
    for (int logical = 0; logical < at; ++logical)
        sections.append(m_sections.at(logical));
    for (int logical = at + removed; logical < total; ++logical)
        sections.append(m_sections.at(logical));
    m_sections = sections;

    QVector<int> visualOrder;
    visualOrder.reserve(m_visualToLogical.size());
    for (int visual = 0; visual < m_visualToLogical.size(); ++visual) {
        const int logical = m_visualToLogical.at(visual);
        if (logical >= at && logical < at + removed)
            continue;                                    // the column is gone
        visualOrder.append(logical >= at + removed ? logical - removed : logical);
    }
    m_visualToLogical = visualOrder;
    ++m_orderRevision;
    rebuildIndexMaps();

    if (m_sortIndicatorSection >= at) {
        if (m_sortIndicatorSection < at + removed) {
            // The sorted column was removed: there is nothing left to indicate.
            m_sortIndicatorSection = -1;
            emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
        } else {
            m_sortIndicatorSection -= removed;
            emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
        }
    }

    invalidateCaches();
    emit sectionCountChanged(sectionCount());
    emitGeometryChanged();
}

int HeaderGeometry::visibleSectionCount() const
{
    int visible = 0;
    for (const Section &section : m_sections) {
        if (!section.hidden && section.size > 0)
            ++visible;
    }
    return visible;
}

int HeaderGeometry::hiddenSectionCount() const
{
    int hidden = 0;
    for (const Section &section : m_sections) {
        if (section.hidden)
            ++hidden;
    }
    return hidden;
}

// ---------------------------------------------------------------------------
// Sizes
// ---------------------------------------------------------------------------

const HeaderGeometry::Section *HeaderGeometry::sectionAt(int logicalIndex) const
{
    if (!isValidLogical(logicalIndex))
        return nullptr;
    return &m_sections.at(logicalIndex);
}

bool HeaderGeometry::isValidLogical(int logicalIndex) const
{
    return logicalIndex >= 0 && logicalIndex < sectionCount();
}

bool HeaderGeometry::isValidVisual(int visualIndex) const
{
    return visualIndex >= 0 && visualIndex < m_visualToLogical.size();
}

int HeaderGeometry::clampedSize(int size) const
{
    const int minimum = qMax(1, m_minimumSectionSize);
    const int maximum = qMax(minimum, m_maximumSectionSize);
    return qBound(minimum, size, maximum);
}

int HeaderGeometry::sectionSize(int logicalIndex) const
{
    const Section *section = sectionAt(logicalIndex);
    if (!section || section->hidden)
        return 0;
    return section->size;
}

int HeaderGeometry::storedSectionSize(int logicalIndex) const
{
    const Section *section = sectionAt(logicalIndex);
    return section ? section->size : 0;
}

void HeaderGeometry::resizeSection(int logicalIndex, int size)
{
    if (!isValidLogical(logicalIndex))
        return;
    const int clamped = clampedSize(size);
    Section &section = m_sections[logicalIndex];
    const int previous = section.size;
    section.explicitSize = true;
    if (previous == clamped)
        return;
    section.size = clamped;
    invalidateCaches();
    emit sectionResized(logicalIndex, previous, clamped);
    emitGeometryChanged();
}

bool HeaderGeometry::isSectionSizeExplicit(int logicalIndex) const
{
    const Section *section = sectionAt(logicalIndex);
    return section && section->explicitSize;
}

void HeaderGeometry::clearExplicitSectionSize(int logicalIndex)
{
    if (!isValidLogical(logicalIndex))
        return;
    m_sections[logicalIndex].explicitSize = false;
}

void HeaderGeometry::setDefaultSectionSize(int size)
{
    const int clamped = clampedSize(size);
    if (m_defaultSectionSize == clamped)
        return;
    m_defaultSectionSize = clamped;
    for (Section &section : m_sections) {
        if (!section.explicitSize)
            section.size = clamped;
    }
    invalidateCaches();
    emit bulkGeometryChanged();
    emitGeometryChanged();
}

void HeaderGeometry::setMinimumSectionSize(int size)
{
    const int clamped = qMax(1, size);
    if (m_minimumSectionSize == clamped)
        return;
    m_minimumSectionSize = clamped;
    if (m_maximumSectionSize < clamped)
        m_maximumSectionSize = clamped;
    clampSectionsToTheSizeRange();
}

void HeaderGeometry::setMaximumSectionSize(int size)
{
    const int clamped = qMax(m_minimumSectionSize, size);
    if (m_maximumSectionSize == clamped)
        return;
    m_maximumSectionSize = clamped;
    clampSectionsToTheSizeRange();
}

void HeaderGeometry::clampSectionsToTheSizeRange()
{
    // Deliberately *not* a loop over resizeSection(): that marks every section
    // explicit (so setDefaultSectionSize() would never affect them again) and
    // emits a full geometryChanged() per section, which turns "change the minimum
    // width" into O(N) renderer syncs. Here the whole bulk change is one pass and,
    // at most, one geometryChanged().
    QVector<QPair<int, QPair<int, int>>> resized;   // logical, old -> new
    resized.reserve(m_sections.size());
    for (int logical = 0; logical < sectionCount(); ++logical) {
        Section &section = m_sections[logical];
        const int clamped = clampedSize(section.size);
        if (clamped == section.size)
            continue;
        resized.append({logical, {section.size, clamped}});
        section.size = clamped;
    }
    // The default follows the range too: otherwise a section created later would
    // start below the minimum (or above the maximum).
    m_defaultSectionSize = clampedSize(m_defaultSectionSize);

    // No early return: this is only called from the two limit setters, so the *range*
    // changed even when no section size had to be clamped - and a renderer keeps its own
    // copy of the range (a Native QHeaderView, for instance, has setMinimumSectionSize() /
    // setMaximumSectionSize()) which it only re-reads on this notification (P1-3 of the
    // second review: changing the minimum without clamping left the native header stale).
    invalidateCaches();
    for (const QPair<int, QPair<int, int>> &entry : resized)
        emit sectionResized(entry.first, entry.second.first, entry.second.second);
    emit bulkGeometryChanged();
    emitGeometryChanged();
}

// ---------------------------------------------------------------------------
// Positions
// ---------------------------------------------------------------------------

void HeaderGeometry::invalidateCaches()
{
    m_cacheDirty = true;
}

void HeaderGeometry::rebuildCaches() const
{
    const int count = int(m_sections.size());
    m_contentXByLogical.resize(count);
    m_visibleLogicalOrder.clear();
    m_visibleLogicalOrder.reserve(count);

    qint64 position = 0;
    for (int visual = 0; visual < m_visualToLogical.size(); ++visual) {
        const int logical = m_visualToLogical.at(visual);
        m_contentXByLogical[logical] = position;
        const Section &section = m_sections.at(logical);
        if (section.hidden)
            continue;
        m_visibleLogicalOrder.append(logical);
        position += section.size;
    }

    m_totalExtent = position;
    m_cacheDirty = false;
}

qint64 HeaderGeometry::totalExtent() const
{
    if (m_cacheDirty)
        rebuildCaches();
    return m_totalExtent;
}

qint64 HeaderGeometry::sectionPosition(int logicalIndex) const
{
    if (!isValidLogical(logicalIndex))
        return 0;
    if (m_cacheDirty)
        rebuildCaches();
    return m_contentXByLogical.at(logicalIndex);
}

int HeaderGeometry::sectionViewportPosition(int logicalIndex) const
{
    const qint64 position = sectionPosition(logicalIndex) - m_viewportOffset;
    return int(qBound<qint64>(qint64(std::numeric_limits<int>::min()),
                              position, qint64(std::numeric_limits<int>::max())));
}

int HeaderGeometry::visualSectionAtOffset(qint64 contentOffset) const
{
    if (m_cacheDirty)
        rebuildCaches();
    if (m_visibleLogicalOrder.isEmpty())
        return -1;
    if (contentOffset < 0)
        return visualIndex(m_visibleLogicalOrder.first());
    if (contentOffset >= m_totalExtent)
        return visualIndex(m_visibleLogicalOrder.last());

    // Binary search over the visible sections (their positions are monotone).
    int low = 0;
    int high = m_visibleLogicalOrder.size() - 1;
    while (low < high) {
        const int mid = (low + high + 1) / 2;
        const int logical = m_visibleLogicalOrder.at(mid);
        if (m_contentXByLogical.at(logical) <= contentOffset)
            low = mid;
        else
            high = mid - 1;
    }
    return visualIndex(m_visibleLogicalOrder.at(low));
}

int HeaderGeometry::sectionAtOffset(qint64 contentOffset) const
{
    const int visual = visualSectionAtOffset(contentOffset);
    return visual < 0 ? -1 : logicalIndex(visual);
}

// ---------------------------------------------------------------------------
// Order / visibility
// ---------------------------------------------------------------------------

int HeaderGeometry::logicalIndex(int visualIndex) const
{
    return isValidVisual(visualIndex) ? m_visualToLogical.at(visualIndex) : -1;
}

int HeaderGeometry::visualIndex(int logicalIndex) const
{
    if (!isValidLogical(logicalIndex) || m_logicalToVisual.size() != sectionCount())
        return -1;
    return m_logicalToVisual.at(logicalIndex);
}

void HeaderGeometry::moveSection(int fromVisualIndex, int toVisualIndex)
{
    if (!isValidVisual(fromVisualIndex) || !isValidVisual(toVisualIndex)
        || fromVisualIndex == toVisualIndex)
        return;

    const int logical = m_visualToLogical.at(fromVisualIndex);
    m_visualToLogical.move(fromVisualIndex, toVisualIndex);
    for (int visual = 0; visual < m_visualToLogical.size(); ++visual)
        m_logicalToVisual[m_visualToLogical.at(visual)] = visual;
    ++m_orderRevision;

    invalidateCaches();
    emit sectionMoved(logical, fromVisualIndex, toVisualIndex);
    emitGeometryChanged();
}

void HeaderGeometry::moveLogicalSections(int start, int count, int destination)
{
    const int total = sectionCount();
    if (count <= 0 || start < 0 || start + count > total)
        return;
    const int clampedDestination = qBound(0, destination, total);
    const int target = clampedDestination > start + count - 1
        ? clampedDestination - count
        : clampedDestination;
    if (target == start)
        return;

    // Permutation of logical indices caused by the model move.
    const auto remap = [start, count, target](int logical) {
        if (logical >= start && logical < start + count)
            return target + (logical - start);
        if (target < start) {
            if (logical >= target && logical < start)
                return logical + count;
        } else if (logical >= start + count && logical < target + count) {
            return logical - count;
        }
        return logical;
    };

    QVector<Section> sections(total);
    for (int logical = 0; logical < total; ++logical)
        sections[remap(logical)] = m_sections.at(logical);
    m_sections = sections;

    for (int visual = 0; visual < m_visualToLogical.size(); ++visual)
        m_visualToLogical[visual] = remap(m_visualToLogical.at(visual));
    // The sort indicator names a column, not a position.
    const int sortBeforeMove = m_sortIndicatorSection;
    m_sortIndicatorSection = remap(m_sortIndicatorSection);
    if (m_sortIndicatorSection != sortBeforeMove)
        emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
    // The logical index of every visual slot changes, so a renderer that caches the order
    // (or the logical index per slot) has to re-derive it - same contract as moveSection()
    // and the insert/remove paths (P1-5 of the second review).
    ++m_orderRevision;
    rebuildIndexMaps();

    invalidateCaches();
    // A model-side move renames many sections at once and has no granular signal of its own
    // (unlike a resize or a visibility toggle), so renderers are told to re-read everything.
    emit bulkGeometryChanged();
    emitGeometryChanged();
}

bool HeaderGeometry::isSectionHidden(int logicalIndex) const
{
    const Section *section = sectionAt(logicalIndex);
    return section && section->hidden;
}

void HeaderGeometry::setSectionHidden(int logicalIndex, bool hidden)
{
    if (!isValidLogical(logicalIndex))
        return;
    Section &section = m_sections[logicalIndex];
    if (section.hidden == hidden)
        return;
    section.hidden = hidden;
    ++m_orderRevision;
    invalidateCaches();
    emit sectionVisibilityChanged(logicalIndex, !hidden);
    emitGeometryChanged();
}

void HeaderGeometry::setAllSectionsHidden(bool hidden)
{
    bool changed = false;
    for (int logical = 0; logical < sectionCount(); ++logical) {
        Section &section = m_sections[logical];
        if (section.hidden == hidden)
            continue;
        section.hidden = hidden;
        changed = true;
        emit sectionVisibilityChanged(logical, !hidden);
    }
    if (!changed)
        return;
    ++m_orderRevision;
    invalidateCaches();
    emitGeometryChanged();
}

// ---------------------------------------------------------------------------
// Horizontal scrolling
// ---------------------------------------------------------------------------

void HeaderGeometry::setViewportOffset(qint64 offset)
{
    const qint64 clamped = qMax<qint64>(0, offset);
    if (m_viewportOffset == clamped)
        return;
    m_viewportOffset = clamped;
    // Only the offset changed: a header can shift without re-reading the
    // sections, and the body re-queries the geometry anyway.
    emit offsetChanged(m_viewportOffset);
}

qint64 HeaderGeometry::maximumViewportOffset(int viewportExtent) const
{
    return qMax<qint64>(0, totalExtent() - qMax(0, viewportExtent));
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

ColumnGeometry HeaderGeometry::columnGeometry(int logicalIndex) const
{
    ColumnGeometry geometry;
    const Section *section = sectionAt(logicalIndex);
    if (!section)
        return geometry;

    geometry.logicalIndex = logicalIndex;
    geometry.visualIndex = visualIndex(logicalIndex);
    geometry.hidden = section->hidden;
    geometry.width = section->hidden ? 0 : section->size;
    geometry.contentX = sectionPosition(logicalIndex);
    // Only the neighbourhood of the window is reported exactly: a section may sit
    // far outside it (a very wide table), and QRect/QWidget arithmetic is 32-bit -
    // Qt even asserts on overflow - so far-away sections collapse to a sentinel.
    const qint64 viewportX = geometry.contentX - m_viewportOffset;
    geometry.viewportX = int(qBound<qint64>(-kMaxOffscreenX, viewportX, kMaxOffscreenX));
    return geometry;
}

VisibleRange HeaderGeometry::visibleVisualRange(int viewportExtent) const
{
    return visibleVisualRangeFor(m_viewportOffset, viewportExtent);
}

VisibleRange HeaderGeometry::visibleVisualRangeFor(qint64 windowStart, int viewportExtent) const
{
    VisibleRange range;
    if (sectionCount() == 0 || viewportExtent <= 0)
        return range;
    const qint64 start = qMax<qint64>(0, windowStart);
    const int first = visualSectionAtOffset(start);
    const int last = visualSectionAtOffset(start + qMax<qint64>(0, viewportExtent - 1));
    if (first < 0 || last < 0)
        return range;
    range.first = qMin(first, last);
    range.last = qMax(first, last);
    return range;
}

QVector<int> HeaderGeometry::visibleSectionsInVisualOrder() const
{
    if (m_cacheDirty)
        rebuildCaches();
    return m_visibleLogicalOrder;
}

// ---------------------------------------------------------------------------
// Sort state / stretch
// ---------------------------------------------------------------------------

void HeaderGeometry::setSortIndicator(int logicalIndex, Qt::SortOrder order)
{
    const int sectionIndex = isValidLogical(logicalIndex) ? logicalIndex : -1;
    if (m_sortIndicatorSection == sectionIndex && m_sortIndicatorOrder == order)
        return;
    m_sortIndicatorSection = sectionIndex;
    m_sortIndicatorOrder = order;
    emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
    emitGeometryChanged();
}

void HeaderGeometry::setStretchLastSection(bool stretch)
{
    if (m_stretchLastSection == stretch)
        return;
    m_stretchLastSection = stretch;
    emit stretchLastSectionChanged(stretch);
    emit bulkGeometryChanged();
    emitGeometryChanged();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

QByteArray HeaderGeometry::saveState() const
{
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << kStateMagic << kStateVersion;
    stream << qint32(m_orientation == Qt::Horizontal ? 1 : 0);
    stream << qint32(sectionCount());

    QVector<qint32> visualOrder;
    visualOrder.reserve(m_visualToLogical.size());
    for (int visual = 0; visual < m_visualToLogical.size(); ++visual)
        visualOrder.append(qint32(m_visualToLogical.at(visual)));
    stream << visualOrder;

    QVector<qint32> sizes;
    sizes.reserve(sectionCount());
    QVector<quint8> hidden;
    hidden.reserve(sectionCount());
    for (const Section &section : m_sections) {
        sizes.append(qint32(section.size));
        hidden.append(section.hidden ? quint8(1) : quint8(0));
    }
    stream << sizes << hidden;

    stream << qint32(m_defaultSectionSize) << qint32(m_minimumSectionSize)
           << qint32(m_maximumSectionSize);
    stream << qint64(m_viewportOffset);
    stream << qint32(m_sortIndicatorSection) << qint32(m_sortIndicatorOrder);
    stream << quint8(m_stretchLastSection ? 1 : 0);
    return state;
}

bool HeaderGeometry::restoreState(const QByteArray &state)
{
    QDataStream stream(state);
    stream.setVersion(QDataStream::Qt_5_15);

    quint32 magic = 0;
    quint32 version = 0;
    qint32 orientation = 0;
    qint32 count = 0;
    stream >> magic >> version >> orientation >> count;
    if (stream.status() != QDataStream::Ok || magic != kStateMagic || version != kStateVersion)
        return false;
    if (sectionCount() != count)
        return false;
    if ((orientation == 1) != (m_orientation == Qt::Horizontal))
        return false;

    QVector<qint32> visualOrder;
    QVector<qint32> sizes;
    QVector<quint8> hidden;
    qint32 defaultSize = 0;
    qint32 minimumSize = 0;
    qint32 maximumSize = 0;
    qint64 offset = 0;
    qint32 sortSection = -1;
    qint32 sortOrder = 0;
    quint8 stretch = 0;
    stream >> visualOrder >> sizes >> hidden >> defaultSize >> minimumSize >> maximumSize
           >> offset >> sortSection >> sortOrder >> stretch;
    if (stream.status() != QDataStream::Ok)
        return false;
    if (visualOrder.size() != count || sizes.size() != count || hidden.size() != count
        || !isValidVisualOrder(visualOrder))
        return false;

    m_defaultSectionSize = qMax(1, int(defaultSize));
    m_minimumSectionSize = qMax(1, int(minimumSize));
    m_maximumSectionSize = qMax(m_minimumSectionSize, int(maximumSize));

    for (int logical = 0; logical < count; ++logical) {
        Section &section = m_sections[logical];
        section.size = clampedSize(int(sizes.at(logical)));
        section.hidden = hidden.at(logical) != 0;
        section.explicitSize = true;
    }
    m_visualToLogical = visualOrder;
    for (int visual = 0; visual < m_visualToLogical.size(); ++visual)
        m_logicalToVisual[m_visualToLogical.at(visual)] = visual;

    m_viewportOffset = qMax<qint64>(0, offset);
    m_sortIndicatorSection = isValidLogical(int(sortSection)) ? int(sortSection) : -1;
    m_sortIndicatorOrder = (sortOrder == int(Qt::AscendingOrder)) ? Qt::AscendingOrder
                                                                  : Qt::DescendingOrder;
    m_stretchLastSection = stretch != 0;

    invalidateCaches();
    emit bulkGeometryChanged();
    emitGeometryChanged();
    return true;
}

bool HeaderGeometry::isValidVisualOrder(const QVector<qint32> &order) const
{
    if (order.size() != sectionCount())
        return false;
    QVector<bool> seen(sectionCount(), false);
    for (qint32 logical : order) {
        if (logical < 0 || logical >= sectionCount() || seen.at(int(logical)))
            return false;
        seen[int(logical)] = true;
    }
    return true;
}

void HeaderGeometry::emitGeometryChanged()
{
    emit geometryChanged();
}

} // namespace viv
