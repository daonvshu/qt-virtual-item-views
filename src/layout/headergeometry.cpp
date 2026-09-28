#include <virtualitemviews/headergeometry.h>

#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <limits>

namespace viv {

// A shared build has to export these public constants (see virtualitemview.cpp).
namespace {
[[maybe_unused]] const void *const volatile kExportedConstants[] = {
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

    if (m_uniformCount > 0) {
        // Uniform: every section is default-sized, visible and in logical order, so a
        // count change is O(1) - apart from dropping the sparse overrides that fell off
        // the end.
        while (!m_sparseKeys.isEmpty() && m_sparseKeys.last() >= clamped) {
            m_sparseKeys.removeLast();
            m_sparseSizes.removeLast();
            m_sparseFactors.removeLast();
        }
        rebuildSparsePrefix();
        m_uniformCount = clamped;
        ++m_orderRevision;              // the visible order changed either way
        if (m_sortIndicatorSection >= clamped) {
            m_sortIndicatorSection = -1;
            emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
        }
        invalidateCaches();
        emit sectionCountChanged(clamped);
        applyStretch();
        emitGeometryChanged();
        return;
    }

    if (m_sections.isEmpty() && clamped > 0) {
        // A growing geometry with nothing to preserve becomes uniform: "count + default
        // size" is the whole state, so a 10M-row table costs O(1) until it edits a single
        // row (which is what makes the row-number strip usable at that scale).
        m_uniformCount = clamped;
        ++m_orderRevision;
        invalidateCaches();
        emit sectionCountChanged(clamped);
        emitGeometryChanged();
        return;
    }

    if (clamped < sectionCount()) {
        const int removedFrom = clamped;
        m_sections.resize(clamped);
        // Drop the removed logical indices from the visual order.
        auto kept = std::remove_if(m_visualToLogical.begin(), m_visualToLogical.end(),
                                   [removedFrom](int logical) { return logical >= removedFrom; });
        m_visualToLogical.erase(kept, m_visualToLogical.end());
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
    applyStretch();
    emitGeometryChanged();
}

void HeaderGeometry::densify()
{
    if (m_uniformCount <= 0)
        return;
    const int count = m_uniformCount;
    m_uniformCount = 0;
    m_sections.resize(count);
    for (Section &section : m_sections)
        section.size = m_defaultSectionSize;
    // The sparse size overrides become ordinary per-section state.
    for (int index = 0; index < m_sparseKeys.size(); ++index) {
        const int logical = m_sparseKeys.at(index);
        if (logical >= 0 && logical < count) {
            m_sections[logical].size = m_sparseSizes.at(index);
            m_sections[logical].stretchFactor = m_sparseFactors.at(index);
            // A stretching section's size is derived from the extent, not a size the user
            // asked for, so it does not become an explicit one on the way.
            m_sections[logical].explicitSize = m_sparseFactors.at(index) <= 0.0;
        }
    }
    m_sparseKeys.clear();
    m_sparseSizes.clear();
    m_sparseFactors.clear();
    m_sparseDeltaPrefix.clear();
    m_visualToLogical.resize(count);
    for (int visual = 0; visual < count; ++visual)
        m_visualToLogical[visual] = visual;
    rebuildIndexMaps();
    invalidateCaches();
}

int HeaderGeometry::sparseIndexOf(int logicalIndex) const
{
    if (m_sparseKeys.isEmpty() || logicalIndex < m_sparseKeys.first()
        || logicalIndex > m_sparseKeys.last()) {
        return -1;
    }
    const auto found = std::lower_bound(m_sparseKeys.cbegin(), m_sparseKeys.cend(), logicalIndex);
    if (found == m_sparseKeys.cend() || *found != logicalIndex)
        return -1;
    return int(found - m_sparseKeys.cbegin());
}

void HeaderGeometry::rebuildSparsePrefix()
{
    m_sparseDeltaPrefix.resize(m_sparseKeys.size() + 1);
    m_sparseDeltaPrefix[0] = 0;
    for (int index = 0; index < m_sparseKeys.size(); ++index) {
        const qint64 delta = qint64(m_sparseSizes.at(index)) - qint64(m_defaultSectionSize);
        m_sparseDeltaPrefix[index + 1] = m_sparseDeltaPrefix.at(index) + delta;
    }
}

qint64 HeaderGeometry::sparseDeltaBefore(int logicalIndex) const
{
    if (m_sparseKeys.isEmpty() || logicalIndex <= m_sparseKeys.first())
        return 0;
    const auto found = std::lower_bound(m_sparseKeys.cbegin(), m_sparseKeys.cend(), logicalIndex);
    const int before = int(found - m_sparseKeys.cbegin());
    return m_sparseDeltaPrefix.at(before);
}

void HeaderGeometry::setSparseSize(int logicalIndex, int size)
{
    const int index = sparseIndexOf(logicalIndex);
    // An entry has to stay while it carries a stretch factor: the factor belongs to the
    // section even when the size it happens to have right now *is* the default.
    const bool stretching = index >= 0 && m_sparseFactors.at(index) > 0.0;
    if (size == m_defaultSectionSize && !stretching) {
        if (index < 0)
            return;                     // already the default: nothing is stored
        m_sparseKeys.remove(index);
        m_sparseSizes.remove(index);
        m_sparseFactors.remove(index);
    } else if (index >= 0) {
        if (m_sparseSizes.at(index) == size)
            return;
        m_sparseSizes[index] = size;
    } else {
        const auto at = std::lower_bound(m_sparseKeys.cbegin(), m_sparseKeys.cend(), logicalIndex);
        const int slot = int(at - m_sparseKeys.cbegin());
        m_sparseKeys.insert(slot, logicalIndex);
        m_sparseSizes.insert(slot, size);
        m_sparseFactors.insert(slot, 0.0);
    }
    rebuildSparsePrefix();
}

void HeaderGeometry::setSparseFactor(int logicalIndex, qreal factor)
{
    const int index = sparseIndexOf(logicalIndex);
    if (factor <= 0.0) {
        if (index < 0)
            return;
        m_sparseFactors[index] = 0.0;
        if (m_sparseSizes.at(index) == m_defaultSectionSize) {
            m_sparseKeys.remove(index);
            m_sparseSizes.remove(index);
            m_sparseFactors.remove(index);
        }
    } else if (index >= 0) {
        if (m_sparseFactors.at(index) == factor)
            return;
        m_sparseFactors[index] = factor;
    } else {
        const auto at = std::lower_bound(m_sparseKeys.cbegin(), m_sparseKeys.cend(), logicalIndex);
        const int slot = int(at - m_sparseKeys.cbegin());
        m_sparseKeys.insert(slot, logicalIndex);
        m_sparseSizes.insert(slot, m_defaultSectionSize);
        m_sparseFactors.insert(slot, factor);
    }
    // Only sizes feed the delta prefix, and the factor itself never moves a position.
    rebuildSparsePrefix();
}

int HeaderGeometry::sparseSlotFor(int logicalIndex)
{
    const int index = sparseIndexOf(logicalIndex);
    if (index >= 0)
        return index;
    const auto at = std::lower_bound(m_sparseKeys.cbegin(), m_sparseKeys.cend(), logicalIndex);
    const int slot = int(at - m_sparseKeys.cbegin());
    m_sparseKeys.insert(slot, logicalIndex);
    m_sparseSizes.insert(slot, m_defaultSectionSize);
    m_sparseFactors.insert(slot, 0.0);
    return slot;
}

void HeaderGeometry::pruneSparseList()
{
    QVector<int> keys;
    QVector<int> sizes;
    QVector<qreal> factors;
    keys.reserve(m_sparseKeys.size());
    sizes.reserve(m_sparseSizes.size());
    factors.reserve(m_sparseFactors.size());
    for (int index = 0; index < m_sparseKeys.size(); ++index) {
        const bool exceptional = m_sparseSizes.at(index) != m_defaultSectionSize
            || m_sparseFactors.at(index) > 0.0;
        if (!exceptional)
            continue;
        keys.append(m_sparseKeys.at(index));
        sizes.append(m_sparseSizes.at(index));
        factors.append(m_sparseFactors.at(index));
    }
    if (keys.size() == m_sparseKeys.size())
        return;
    m_sparseKeys = keys;
    m_sparseSizes = sizes;
    m_sparseFactors = factors;
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
    if (sectionCount() == 0) {
        setSectionCount(count);
        return;
    }
    if (m_uniformCount > 0) {
        const int at = qBound(0, first, m_uniformCount);
        for (int &key : m_sparseKeys) {
            if (key >= at)
                key += count;
        }
        m_uniformCount += count;
        rebuildSparsePrefix();
        if (m_sortIndicatorSection >= at) {
            m_sortIndicatorSection += count;
            emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
        }
        ++m_orderRevision;
        invalidateCaches();
        emit sectionCountChanged(m_uniformCount);
        applyStretch();
        emitGeometryChanged();
        return;
    }
    densify();   // an insert gives sections state to keep
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
    applyStretch();
    emitGeometryChanged();
}

void HeaderGeometry::removeLogicalSections(int first, int count)
{
    if (count <= 0)
        return;
    if (m_uniformCount > 0) {
        const int at = qBound(0, first, m_uniformCount);
        const int removed = qMin(count, m_uniformCount - at);
        if (removed <= 0)
            return;
        QVector<int> keys;
        QVector<int> sizes;
        QVector<qreal> factors;
        for (int index = 0; index < m_sparseKeys.size(); ++index) {
            const int key = m_sparseKeys.at(index);
            if (key >= at && key < at + removed)
                continue;
            keys.append(key >= at + removed ? key - removed : key);
            sizes.append(m_sparseSizes.at(index));
            factors.append(m_sparseFactors.at(index));
        }
        m_sparseKeys = keys;
        m_sparseSizes = sizes;
        m_sparseFactors = factors;
        m_uniformCount -= removed;
        rebuildSparsePrefix();
        if (m_sortIndicatorSection >= at) {
            m_sortIndicatorSection = m_sortIndicatorSection < at + removed
                ? -1 : m_sortIndicatorSection - removed;
            emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
        }
        ++m_orderRevision;
        invalidateCaches();
        emit sectionCountChanged(m_uniformCount);
        applyStretch();
        emitGeometryChanged();
        return;
    }
    densify();   // dropping sections needs the stored state
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
    applyStretch();
    emitGeometryChanged();
}

int HeaderGeometry::visibleSectionCount() const
{
    if (m_uniformCount > 0)
        return m_uniformCount;   // every implied section is visible
    int visible = 0;
    for (const Section &section : m_sections) {
        if (!section.hidden && section.size > 0)
            ++visible;
    }
    return visible;
}

int HeaderGeometry::hiddenSectionCount() const
{
    if (m_uniformCount > 0)
        return 0;
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
    if (!isValidLogical(logicalIndex) || m_uniformCount > 0)
        return nullptr;   // uniform: the state is implied, nothing is stored
    return &m_sections.at(logicalIndex);
}

bool HeaderGeometry::isValidLogical(int logicalIndex) const
{
    return logicalIndex >= 0 && logicalIndex < sectionCount();
}

bool HeaderGeometry::isValidVisual(int visualIndex) const
{
    // sectionCount() covers the uniform representation too (where no map is stored).
    return visualIndex >= 0 && visualIndex < sectionCount()
        && (m_uniformCount > 0 || m_visualToLogical.size() == sectionCount());
}

int HeaderGeometry::clampedSize(int size) const
{
    const int minimum = qMax(1, m_minimumSectionSize);
    const int maximum = qMax(minimum, m_maximumSectionSize);
    return qBound(minimum, size, maximum);
}

int HeaderGeometry::sectionSize(int logicalIndex) const
{
    if (m_uniformCount > 0) {
        if (!isValidLogical(logicalIndex))
            return 0;
        const int index = sparseIndexOf(logicalIndex);
        return index >= 0 ? m_sparseSizes.at(index) : m_defaultSectionSize;
    }
    const Section *section = sectionAt(logicalIndex);
    if (!section || section->hidden)
        return 0;
    return section->size;
}

int HeaderGeometry::storedSectionSize(int logicalIndex) const
{
    if (m_uniformCount > 0)
        return sectionSize(logicalIndex);
    const Section *section = sectionAt(logicalIndex);
    return section ? section->size : 0;
}

void HeaderGeometry::resizeSection(int logicalIndex, int size)
{
    if (!isValidLogical(logicalIndex))
        return;
    const int clamped = clampedSize(size);
    if (m_uniformCount > 0) {
        // Uniform: a size override is k entries in the sparse list, not the whole section
        // set - this is the row-boundary drag at ten million rows (§5/§12).
        const int index = sparseIndexOf(logicalIndex);
        const int previous = index >= 0 ? m_sparseSizes.at(index) : m_defaultSectionSize;
        // Dragging a section's edge takes it out of the stretch pass: from now on its
        // width is the one the user dragged (QHeaderView's Stretch -> Interactive).
        const bool wasStretching = index >= 0 && m_sparseFactors.at(index) > 0.0;
        if (wasStretching)
            setSparseFactor(logicalIndex, 0.0);
        if (previous == clamped) {
            if (wasStretching) {
                invalidateCaches();
                applyStretch();      // the remaining participants share what it gave up
                emitGeometryChanged();
            }
            return;
        }
        setSparseSize(logicalIndex, clamped);
        invalidateCaches();
        emit sectionResized(logicalIndex, previous, clamped);
        applyStretch();
        emitGeometryChanged();
        return;
    }
    densify();
    Section &section = m_sections[logicalIndex];
    const int previous = section.size;
    const bool wasStretching = section.stretchFactor > 0.0;
    section.stretchFactor = 0.0;
    section.explicitSize = true;
    if (previous == clamped) {
        if (wasStretching) {
            invalidateCaches();
            applyStretch();
            emitGeometryChanged();
        }
        return;
    }
    section.size = clamped;
    invalidateCaches();
    emit sectionResized(logicalIndex, previous, clamped);
    applyStretch();
    emitGeometryChanged();
}

bool HeaderGeometry::isSectionSizeExplicit(int logicalIndex) const
{
    if (m_uniformCount > 0) {
        // A stretching entry keeps the section's size in the sparse list but the size is
        // derived from the extent, so it is not an explicit one.
        const int index = sparseIndexOf(logicalIndex);
        return index >= 0 && m_sparseFactors.at(index) <= 0.0;
    }
    const Section *section = sectionAt(logicalIndex);
    return section && section->explicitSize;
}

void HeaderGeometry::clearExplicitSectionSize(int logicalIndex)
{
    if (!isValidLogical(logicalIndex))
        return;
    if (m_uniformCount > 0) {
        // Back to the implied size, which *is* a size change here (the indexed path only
        // drops the flag and keeps the size).
        const int index = sparseIndexOf(logicalIndex);
        if (index < 0)
            return;
        const int previous = m_sparseSizes.at(index);
        setSparseSize(logicalIndex, m_defaultSectionSize);
        invalidateCaches();
        emit sectionResized(logicalIndex, previous, m_defaultSectionSize);
        applyStretch();
        emitGeometryChanged();
        return;
    }
    m_sections[logicalIndex].explicitSize = false;
}

void HeaderGeometry::clearExplicitSectionSizes()
{
    if (m_uniformCount > 0) {
        bool changed = false;
        QVector<int> keys;
        QVector<int> sizes;
        QVector<qreal> factors;
        for (int index = 0; index < m_sparseKeys.size(); ++index) {
            const qreal factor = m_sparseFactors.at(index);
            if (factor <= 0.0) {
                changed = true;
                continue;
            }
            keys.append(m_sparseKeys.at(index));
            sizes.append(m_sparseSizes.at(index));
            factors.append(factor);
        }
        if (!changed)
            return;
        m_sparseKeys = keys;
        m_sparseSizes = sizes;
        m_sparseFactors = factors;
        rebuildSparsePrefix();
        invalidateCaches();
        applyStretch();
        emit bulkGeometryChanged();
        emitGeometryChanged();
        return;
    }
    bool changed = false;
    for (Section &section : m_sections) {
        if (!section.explicitSize)
            continue;
        section.size = m_defaultSectionSize;
        section.explicitSize = false;
        changed = true;
    }
    if (changed) {
        invalidateCaches();
        emit bulkGeometryChanged();
    }
    applyStretch();
    if (changed)
        emitGeometryChanged();
}
void HeaderGeometry::setDefaultSectionSize(int size)
{
    const int clamped = clampedSize(size);
    if (m_defaultSectionSize == clamped)
        return;
    m_defaultSectionSize = clamped;
    if (m_uniformCount > 0) {
        // Every non-overridden section follows the default (the overrides keep the size
        // the user gave them, and their deltas are relative to the default).
        rebuildSparsePrefix();
        invalidateCaches();
        emit bulkGeometryChanged();
        applyStretch();
        emitGeometryChanged();
        return;
    }
    for (Section &section : m_sections) {
        if (!section.explicitSize)
            section.size = clamped;
    }
    invalidateCaches();
    emit bulkGeometryChanged();
    applyStretch();
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
    if (m_uniformCount > 0) {
        // Uniform: the sections follow the default size, so clamping the range is one
        // assignment - plus the (few) explicit sizes - and the range change still has to
        // be reported (a renderer keeps its own copy of it).
        m_defaultSectionSize = clampedSize(m_defaultSectionSize);
        QVector<QPair<int, QPair<int, int>>> clampedOverrides;
        for (int index = 0; index < m_sparseSizes.size(); ++index) {
            const int clamped = clampedSize(m_sparseSizes.at(index));
            if (clamped == m_sparseSizes.at(index))
                continue;
            clampedOverrides.append({m_sparseKeys.at(index),
                                     {m_sparseSizes.at(index), clamped}});
            m_sparseSizes[index] = clamped;
        }
        rebuildSparsePrefix();
        invalidateCaches();
        for (const QPair<int, QPair<int, int>> &entry : clampedOverrides)
            emit sectionResized(entry.first, entry.second.first, entry.second.second);
        emit bulkGeometryChanged();
        applyStretch();
        emitGeometryChanged();
        return;
    }

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
    applyStretch();
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
    if (m_uniformCount > 0) {
        // A uniform geometry must not build a per-section cache at all: the extent is
        // one multiplication and the position of a section is one too (see below). The
        // identity order means no lookup table is needed either.
        m_contentXByLogical.clear();
        m_visibleLogicalOrder.clear();
        m_totalExtent = qint64(m_uniformCount) * qint64(m_defaultSectionSize)
            + m_sparseDeltaPrefix.value(m_sparseKeys.size(), 0);
        m_cacheDirty = false;
        return;
    }
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
    if (m_uniformCount > 0) {
        return qint64(logicalIndex) * qint64(m_defaultSectionSize)
            + sparseDeltaBefore(logicalIndex);
    }
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
    if (m_uniformCount > 0) {
        // Identity order and one uniform size: the section is one division away - but the
        // sparse size overrides shift the positions, so the (short) override list decides
        // which region the offset falls into.
        if (contentOffset <= 0)
            return 0;
        int index = 0;
        qint64 position = 0;
        for (int slot = 0; slot < m_sparseKeys.size(); ++slot) {
            const int key = m_sparseKeys.at(slot);
            const qint64 keyStart = qint64(key) * qint64(m_defaultSectionSize)
                + m_sparseDeltaPrefix.at(slot);
            if (keyStart > contentOffset)
                break;
            if (contentOffset < keyStart + m_sparseSizes.at(slot))
                return key;
            index = key + 1;
            position = keyStart + m_sparseSizes.at(slot);
        }
        if (index >= m_uniformCount)
            return m_uniformCount - 1;
        const qint64 ahead = (contentOffset - position) / qint64(m_defaultSectionSize);
        return int(qBound<qint64>(qint64(index), qint64(index) + ahead, qint64(m_uniformCount) - 1));
    }
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
    if (m_uniformCount > 0)
        return isValidVisual(visualIndex) ? visualIndex : -1;
    return isValidVisual(visualIndex) ? m_visualToLogical.at(visualIndex) : -1;
}

int HeaderGeometry::visualIndex(int logicalIndex) const
{
    if (m_uniformCount > 0)
        return isValidLogical(logicalIndex) ? logicalIndex : -1;
    if (!isValidLogical(logicalIndex) || m_logicalToVisual.size() != sectionCount())
        return -1;
    return m_logicalToVisual.at(logicalIndex);
}

void HeaderGeometry::moveSection(int fromVisualIndex, int toVisualIndex)
{
    if (!isValidVisual(fromVisualIndex) || !isValidVisual(toVisualIndex)
        || fromVisualIndex == toVisualIndex)
        return;
    densify();   // a reorder is per-section state

    const int logical = m_visualToLogical.at(fromVisualIndex);
    m_visualToLogical.move(fromVisualIndex, toVisualIndex);
    for (int visual = 0; visual < m_visualToLogical.size(); ++visual)
        m_logicalToVisual[m_visualToLogical.at(visual)] = visual;
    ++m_orderRevision;

    invalidateCaches();
    emit sectionMoved(logical, fromVisualIndex, toVisualIndex);
    applyStretch();     // "the last visible section" may have become a different one
    emitGeometryChanged();
}

void HeaderGeometry::moveLogicalSections(int start, int count, int destination)
{
    densify();   // a remap is per-section state
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
    applyStretch();
    emitGeometryChanged();
}

void HeaderGeometry::moveLogicalSectionSizes(int start, int count, int destination)
{
    if (m_uniformCount <= 0) {
        moveLogicalSections(start, count, destination);
        return;
    }
    const int total = m_uniformCount;
    if (count <= 0 || start < 0 || count > total - start)
        return;
    const int to = qBound(0, destination, total);
    if (to >= start && to <= start + count)
        return;
    const int target = to > start ? to - count : to;

    struct Entry { int key; int size; qreal factor; };
    QVector<Entry> entries;
    entries.reserve(m_sparseKeys.size());
    for (int index = 0; index < m_sparseKeys.size(); ++index) {
        int key = m_sparseKeys.at(index);
        if (key >= start && key < start + count)
            key = target + key - start;
        else if (target < start && key >= target && key < start)
            key += count;
        else if (target > start && key >= start + count && key < to)
            key -= count;
        entries.append({key, m_sparseSizes.at(index), m_sparseFactors.at(index)});
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry &lhs, const Entry &rhs) { return lhs.key < rhs.key; });
    for (int index = 0; index < entries.size(); ++index) {
        m_sparseKeys[index] = entries.at(index).key;
        m_sparseSizes[index] = entries.at(index).size;
        m_sparseFactors[index] = entries.at(index).factor;
    }
    if (m_sortIndicatorSection >= 0) {
        const int old = m_sortIndicatorSection;
        if (old >= start && old < start + count)
            m_sortIndicatorSection = target + old - start;
        else if (target < start && old >= target && old < start)
            m_sortIndicatorSection += count;
        else if (target > start && old >= start + count && old < to)
            m_sortIndicatorSection -= count;
        if (m_sortIndicatorSection != old)
            emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
    }
    rebuildSparsePrefix();
    ++m_orderRevision;
    invalidateCaches();
    emit bulkGeometryChanged();
    applyStretch();
    emitGeometryChanged();
}

bool HeaderGeometry::isSectionHidden(int logicalIndex) const
{
    if (m_uniformCount > 0)
        return false;   // a uniform geometry stores no hidden section
    const Section *section = sectionAt(logicalIndex);
    return section && section->hidden;
}

void HeaderGeometry::setSectionHidden(int logicalIndex, bool hidden)
{
    if (!isValidLogical(logicalIndex))
        return;
    if (m_uniformCount > 0 && !hidden)
        return;   // already visible
    densify();
    Section &section = m_sections[logicalIndex];
    if (section.hidden == hidden)
        return;
    section.hidden = hidden;
    ++m_orderRevision;
    invalidateCaches();
    emit sectionVisibilityChanged(logicalIndex, !hidden);
    applyStretch();     // a hidden section leaves the pass and frees its share
    emitGeometryChanged();
}

void HeaderGeometry::setAllSectionsHidden(bool hidden)
{
    if (m_uniformCount > 0 && !hidden)
        return;   // already visible
    densify();
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
    applyStretch();
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
    if (m_uniformCount > 0) {
        if (!isValidLogical(logicalIndex))
            return geometry;
        // Uniform: default size, visible, identity order - all implied.
        geometry.logicalIndex = logicalIndex;
        geometry.visualIndex = logicalIndex;
        geometry.width = sectionSize(logicalIndex);
        geometry.contentX = sectionPosition(logicalIndex);
        const qint64 viewportX = geometry.contentX - m_viewportOffset;
        geometry.viewportX = int(qBound<qint64>(-kMaxOffscreenX, viewportX, kMaxOffscreenX));
        return geometry;
    }
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
    if (m_uniformCount > 0) {
        // Every section is visible and in logical order. The vector is the query, so a
        // huge uniform geometry pays for it here - the renderers never call this.
        QVector<int> order;
        order.reserve(m_uniformCount);
        for (int logical = 0; logical < m_uniformCount; ++logical)
            order.append(logical);
        return order;
    }
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
    applyStretch();
    emit stretchLastSectionChanged(stretch);
    emit bulkGeometryChanged();
    emitGeometryChanged();
}

void HeaderGeometry::setStretchExtent(qint64 extent)
{
    const qint64 clamped = qMax<qint64>(0, extent);
    if (m_stretchExtent == clamped)
        return;
    m_stretchExtent = clamped;
    // Resizing the view is the one change that only moves the target: the sections are
    // re-measured against it, and a geometry without participants stays untouched.
    if (applyStretch())
        emitGeometryChanged();
}

qreal HeaderGeometry::sectionStretchFactor(int logicalIndex) const
{
    if (m_uniformCount > 0) {
        const int index = sparseIndexOf(logicalIndex);
        return index >= 0 ? m_sparseFactors.at(index) : 0.0;
    }
    const Section *section = sectionAt(logicalIndex);
    return section ? section->stretchFactor : 0.0;
}

void HeaderGeometry::setSectionStretchFactor(int logicalIndex, qreal factor)
{
    if (!isValidLogical(logicalIndex))
        return;
    const qreal wanted = factor > 0.0 ? factor : 0.0;
    if (qFuzzyCompare(sectionStretchFactor(logicalIndex) + 1.0, wanted + 1.0))
        return;

    if (m_uniformCount > 0) {
        // One sparse entry per stretching section: "ten million rows, two of them share
        // the leftover" costs two entries, not ten million Sections.
        setSparseFactor(logicalIndex, wanted);
    } else {
        Section &section = m_sections[logicalIndex];
        section.stretchFactor = wanted;
        if (wanted > 0.0)
            section.explicitSize = false;   // the size is derived from now on
    }
    invalidateCaches();
    applyStretch();
    emit sectionStretchFactorChanged(logicalIndex, wanted);
    emitGeometryChanged();
}

bool HeaderGeometry::hasStretchSections() const
{
    // Without a target nothing stretches, whatever factors are configured: a caller can
    // read this as "the stretch state can change a size right now".
    if (m_stretchExtent <= 0)
        return false;
    if (m_stretchLastSection)
        return visibleSectionCount() > 0;
    if (m_uniformCount > 0) {
        for (qreal factor : m_sparseFactors) {
            if (factor > 0.0)
                return true;
        }
        return false;
    }
    for (const Section &section : m_sections) {
        if (!section.hidden && section.stretchFactor > 0.0)
            return true;
    }
    return false;
}

bool HeaderGeometry::applyStretch()
{
    if (m_stretchExtent <= 0 || sectionCount() <= 0)
        return false;

    // 1. The participants, in visual order (that is what makes "the last one absorbs the
    //    rounding" - and stretchLastSection()'s "last visible" - mean the same thing for
    //    both representations), plus the extent the other visible sections keep.
    QVector<int> participants;
    QVector<qreal> factors;
    QVector<int> sizes;
    qint64 fixedTotal = 0;

    if (m_uniformCount > 0) {
        // Uniform: identity order and no hidden section, so the sparse list already is the
        // list of exceptional sections and the pass never walks the section count.
        for (int index = 0; index < m_sparseKeys.size(); ++index) {
            if (m_sparseFactors.at(index) <= 0.0)
                continue;
            participants.append(m_sparseKeys.at(index));
            factors.append(m_sparseFactors.at(index));
            sizes.append(m_sparseSizes.at(index));
        }
        if (m_stretchLastSection) {
            const int last = m_uniformCount - 1;
            if (!participants.contains(last)) {
                const int index = sparseIndexOf(last);
                participants.append(last);
                factors.append(1.0);
                sizes.append(index >= 0 ? m_sparseSizes.at(index) : m_defaultSectionSize);
            }
        }
        if (participants.isEmpty())
            return false;
        qint64 participantTotal = 0;
        for (int size : sizes)
            participantTotal += size;
        fixedTotal = totalExtent() - participantTotal;
    } else {
        if (m_cacheDirty)
            rebuildCaches();
        for (int visual = 0; visual < m_visibleLogicalOrder.size(); ++visual) {
            const int logical = m_visibleLogicalOrder.at(visual);
            const Section &section = m_sections.at(logical);
            if (section.stretchFactor <= 0.0)
                continue;
            participants.append(logical);
            factors.append(section.stretchFactor);
            sizes.append(section.size);
        }
        if (m_stretchLastSection && !m_visibleLogicalOrder.isEmpty()) {
            const int last = m_visibleLogicalOrder.last();
            if (!participants.contains(last)) {
                participants.append(last);
                factors.append(1.0);
                sizes.append(m_sections.at(last).size);
            }
        }
        if (participants.isEmpty())
            return false;
        qint64 participantTotal = 0;
        for (int size : sizes)
            participantTotal += size;
        fixedTotal = totalExtent() - participantTotal;
    }

    qreal totalFactor = 0.0;
    for (qreal factor : factors)
        totalFactor += factor;
    if (totalFactor <= 0.0)
        return false;

    // 2. Split the leftover. The last participant takes the remainder of the integer
    //    division, so the stretched sections add up to the extent whenever the size range
    //    allows it; the range itself still wins (a minimum can make them not fit).
    const qint64 leftover = m_stretchExtent - fixedTotal;
    const qint64 maxSize = qint64(std::numeric_limits<int>::max());
    QVector<int> targets;
    targets.resize(participants.size());
    qint64 assigned = 0;
    for (int index = 0; index + 1 < participants.size(); ++index) {
        const qint64 share = leftover > 0
            ? qint64(qreal(leftover) * factors.at(index) / totalFactor)
            : 0;
        targets[index] = clampedSize(int(qBound<qint64>(qint64(0), share, maxSize)));
        assigned += targets.at(index);
    }
    targets[participants.size() - 1]
        = clampedSize(int(qBound<qint64>(qint64(0), leftover - assigned, maxSize)));

    // 3. Write the sizes that moved.
    bool changed = false;
    if (m_uniformCount > 0) {
        for (int index = 0; index < participants.size(); ++index) {
            if (targets.at(index) == sizes.at(index))
                continue;
            m_sparseSizes[sparseSlotFor(participants.at(index))] = targets.at(index);
            changed = true;
        }
        if (changed) {
            pruneSparseList();
            rebuildSparsePrefix();
        }
    } else {
        for (int index = 0; index < participants.size(); ++index) {
            Section &section = m_sections[participants.at(index)];
            if (section.size == targets.at(index))
                continue;
            section.size = targets.at(index);
            changed = true;
        }
    }
    if (!changed)
        return false;

    invalidateCaches();
    for (int index = 0; index < participants.size(); ++index) {
        if (targets.at(index) == sizes.at(index))
            continue;
        emit sectionResized(participants.at(index), sizes.at(index), targets.at(index));
    }
    return true;
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

    if (m_uniformCount > 0) {
        stream << quint8(1);
        QVector<qint32> keys;
        QVector<qint32> sizes;
        keys.reserve(m_sparseKeys.size());
        sizes.reserve(m_sparseSizes.size());
        for (int key : m_sparseKeys)
            keys.append(qint32(key));
        for (int size : m_sparseSizes)
            sizes.append(qint32(size));
        stream << keys << sizes << m_sparseFactors;
    } else {
        stream << quint8(0);
        QVector<qint32> visualOrder;
        QVector<qint32> sizes;
        QVector<quint8> hidden;
        visualOrder.reserve(m_visualToLogical.size());
        for (int visual = 0; visual < m_visualToLogical.size(); ++visual)
            visualOrder.append(qint32(m_visualToLogical.at(visual)));
        sizes.reserve(sectionCount());
        hidden.reserve(sectionCount());
        for (const Section &section : m_sections) {
            sizes.append(qint32(section.size));
            hidden.append(section.hidden ? quint8(1) : quint8(0));
        }
        QVector<qint32> stretchKeys;
        QVector<qreal> stretchFactors;
        for (int logical = 0; logical < sectionCount(); ++logical) {
            const qreal factor = m_sections.at(logical).stretchFactor;
            if (factor <= 0.0)
                continue;
            stretchKeys.append(qint32(logical));
            stretchFactors.append(factor);
        }
        stream << visualOrder << sizes << hidden << stretchKeys << stretchFactors;
    }

    stream << qint32(m_defaultSectionSize) << qint32(m_minimumSectionSize)
           << qint32(m_maximumSectionSize);
    stream << qint64(m_viewportOffset);
    stream << qint32(m_sortIndicatorSection) << qint32(m_sortIndicatorOrder);
    stream << quint8(m_stretchLastSection ? 1 : 0);
    return state;
}

bool HeaderGeometry::restoreState(const QByteArray &state)
{
    // Captured before the commit so a replaced sort state can be reported after it.
    const int sortBeforeRestore = m_sortIndicatorSection;
    const Qt::SortOrder orderBeforeRestore = m_sortIndicatorOrder;
    QDataStream stream(state);
    stream.setVersion(QDataStream::Qt_5_15);

    quint32 magic = 0;
    quint32 version = 0;
    qint32 orientation = 0;
    qint32 count = 0;
    stream >> magic >> version >> orientation >> count;
    if (stream.status() != QDataStream::Ok || magic != kStateMagic
        || (version != 2 && version != kStateVersion))
        return false;
    if (sectionCount() != count)
        return false;
    if ((orientation == 1) != (m_orientation == Qt::Horizontal))
        return false;

    QVector<qint32> visualOrder;
    QVector<qint32> sizes;
    QVector<quint8> hidden;
    QVector<qint32> sparseKeys;
    QVector<qint32> sparseSizes;
    QVector<qreal> sparseFactors;
    qint32 defaultSize = 0;
    qint32 minimumSize = 0;
    qint32 maximumSize = 0;
    qint64 offset = 0;
    qint32 sortSection = -1;
    qint32 sortOrder = 0;
    quint8 stretch = 0;
    QVector<qint32> stretchKeys;
    QVector<qreal> stretchFactors;
    quint8 compact = 0;
    if (version >= 3)
        stream >> compact;
    if (compact > 1)
        return false;
    if (compact)
        stream >> sparseKeys >> sparseSizes >> sparseFactors;
    else
        stream >> visualOrder >> sizes >> hidden >> stretchKeys >> stretchFactors;
    stream >> defaultSize >> minimumSize >> maximumSize >> offset >> sortSection >> sortOrder
           >> stretch;
    if (stream.status() != QDataStream::Ok)
        return false;
    if (compact) {
        if (sparseKeys.size() != sparseSizes.size()
            || sparseKeys.size() != sparseFactors.size())
            return false;
        for (int index = 0; index < sparseKeys.size(); ++index) {
            if (sparseKeys.at(index) < 0 || sparseKeys.at(index) >= count
                || (index > 0 && sparseKeys.at(index) <= sparseKeys.at(index - 1)))
                return false;
        }
    } else {
        if (visualOrder.size() != count || sizes.size() != count || hidden.size() != count
            || stretchKeys.size() != stretchFactors.size() || !isValidVisualOrder(visualOrder))
            return false;
        for (qint32 logical : stretchKeys) {
            if (!isValidLogical(int(logical)))
                return false;
        }
    }

    if (!compact)
        densify();
    m_defaultSectionSize = qMax(1, int(defaultSize));
    m_minimumSectionSize = qMax(1, int(minimumSize));
    m_maximumSectionSize = qMax(m_minimumSectionSize, int(maximumSize));

    if (compact) {
        m_sections.clear();
        m_visualToLogical.clear();
        m_logicalToVisual.clear();
        m_uniformCount = count;
        m_sparseKeys.clear();
        m_sparseSizes.clear();
        m_sparseFactors.clear();
        for (int index = 0; index < sparseKeys.size(); ++index) {
            m_sparseKeys.append(int(sparseKeys.at(index)));
            m_sparseSizes.append(clampedSize(int(sparseSizes.at(index))));
            m_sparseFactors.append(sparseFactors.at(index));
        }
        rebuildSparsePrefix();
    } else {
        for (int logical = 0; logical < count; ++logical) {
            Section &section = m_sections[logical];
            section.size = clampedSize(int(sizes.at(logical)));
            section.hidden = hidden.at(logical) != 0;
            section.explicitSize = true;
            section.stretchFactor = 0.0;
        }
        for (int index = 0; index < stretchKeys.size(); ++index) {
            Section &section = m_sections[int(stretchKeys.at(index))];
            section.stretchFactor = stretchFactors.at(index);
            if (section.stretchFactor > 0.0)
                section.explicitSize = false;
        }
        m_visualToLogical = visualOrder;
        for (int visual = 0; visual < m_visualToLogical.size(); ++visual)
            m_logicalToVisual[m_visualToLogical.at(visual)] = visual;
    }

    m_viewportOffset = qMax<qint64>(0, offset);
    m_sortIndicatorSection = isValidLogical(int(sortSection)) ? int(sortSection) : -1;
    m_sortIndicatorOrder = (sortOrder == int(Qt::AscendingOrder)) ? Qt::AscendingOrder
                                                                  : Qt::DescendingOrder;
    m_stretchLastSection = stretch != 0;

    // The restored order / hidden set are a structural change, so the revision has to move
    // (a renderer caches the visual order by revision, and the order may just have been
    // replaced wholesale), and a sort state that differs from the live one is reported
    // through the signal a renderer builds its sort UI from (P1 of the third review).
    ++m_orderRevision;
    invalidateCaches();
    if (m_sortIndicatorSection != sortBeforeRestore
        || m_sortIndicatorOrder != orderBeforeRestore) {
        emit sortIndicatorChanged(m_sortIndicatorSection, m_sortIndicatorOrder);
    }
    emit bulkGeometryChanged();
    // The restored sizes are measured against the extent of the view that restores them.
    applyStretch();
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
