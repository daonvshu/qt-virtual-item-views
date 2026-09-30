#pragma once

#include <QByteArray>
#include <QObject>
#include <QPair>
#include <QVector>
#include <Qt>
#include <QtGlobal>

#include <virtualitemviews/global.h>
#include <virtualitemviews/types.h>

namespace viv {

/// Committed geometry of one column (architecture document §20).
///
/// It is *derived* from HeaderGeometry; business row widgets must not compute
/// column positions themselves.
struct ColumnGeometry
{
    int logicalIndex = -1;
    int visualIndex = -1;
    /// Position in content coordinates.
    qint64 contentX = 0;
    /// Position in viewport coordinates (contentX - viewportOffset).
    int viewportX = 0;
    /// 0 for hidden sections.
    int width = 0;
    bool hidden = false;

    bool isValid() const { return logicalIndex >= 0; }
};

/// Single source of truth for column geometry and column state (§14).
///
/// The table body and widget header renderers consume this object; neither keeps
/// an authoritative width, order or visibility copy of its own (§45.10).
class VIRTUALITEMVIEWS_EXPORT HeaderGeometry : public QObject
{
    Q_OBJECT

public:
    explicit HeaderGeometry(Qt::Orientation orientation = Qt::Horizontal,
                            QObject *parent = nullptr);
    ~HeaderGeometry() override;

    Qt::Orientation orientation() const { return m_orientation; }

    // -- section set ---------------------------------------------------------
    /// How many sections the geometry *has*.
    ///
    /// This is deliberately independent of how many per-section states are stored: a
    /// geometry with identity order and no hidden sections stays compact. Size changes
    /// are sparse; visibility and visual-order edits materialize per-section state.
    int sectionCount() const { return m_uniformCount > 0 ? m_uniformCount : int(m_sections.size()); }
    /// Diagnostics: per-section states actually stored. Compact geometry stores only
    /// exceptional sizes and stretch factors, even at very large section counts.
    int storedSectionStateCount() const
    {
        return m_uniformCount > 0 ? int(m_sparseKeys.size()) : int(m_sections.size());
    }
    /// True while the geometry uses identity visual order and no hidden sections.
    /// Sparse size overrides do not change this compact representation.
    bool isUniform() const { return m_uniformCount > 0; }
    /// Grows/shrinks the section set at the end. Model structure changes should
    /// use the logical insert/remove/move calls below instead: they keep the
    /// per-section state (width, visibility, explicit size) with the item it
    /// describes, which appending can never do.
    void setSectionCount(int count);
    /// `columnsInserted(parent, first, last)`: the new sections carry the default
    /// state and take the visual slots of the section that followed them (the one
    /// that used to sit at \a first), like QHeaderView: a header in logical order
    /// stays in logical order, a custom order keeps its shape. Every section after
    /// \a first keeps its own state.
    void insertLogicalSections(int first, int count);
    /// `columnsRemoved(parent, first, last)`: the sections of the removed columns
    /// are dropped, the ones after them keep their state and shift down.
    void removeLogicalSections(int first, int count);
    int visibleSectionCount() const;
    int hiddenSectionCount() const;
    bool isEmpty() const { return sectionCount() == 0; }

    // -- sizes ---------------------------------------------------------------
    /// Size of a section in content coordinates; 0 when hidden/out of range.
    int sectionSize(int logicalIndex) const;
    /// Size of a section even when it is hidden.
    int storedSectionSize(int logicalIndex) const;
    void resizeSection(int logicalIndex, int size);
    /// True when the size was set explicitly (user resize or restoreState).
    bool isSectionSizeExplicit(int logicalIndex) const;
    void clearExplicitSectionSize(int logicalIndex);
    /// Forgets every explicitly set size (the sections go back to the default). Used by a
    /// view that mirrors its own row heights and has to start from a clean slate: with the
    /// sparse representation the overrides that are no longer backed by an explicit height
    /// would otherwise stay behind.
    void clearExplicitSectionSizes();

    int defaultSectionSize() const { return m_defaultSectionSize; }
    void setDefaultSectionSize(int size);
    /// Blank pixels between visible sections; excluded after the last section.
    int sectionSpacing() const { return m_sectionSpacing; }
    void setSectionSpacing(int pixels);
    /// Replaces the spacing after each logical section. Empty restores sectionSpacing().
    /// Overrides are a transient layout projection and clear on structure changes.
    void setSectionSpacingOverrides(const QVector<int> &spacings);
    /// Sorted logical-index/spacing pairs for sparse row gaps. Entries equal to the
    /// default spacing are omitted; structure changes clear this projection too.
    void setSparseSectionSpacingOverrides(const QVector<QPair<int, int>> &spacings);
    int sectionSpacingAfter(int logicalIndex) const;
    int minimumSectionSize() const { return m_minimumSectionSize; }
    void setMinimumSectionSize(int size);
    int maximumSectionSize() const { return m_maximumSectionSize; }
    void setMaximumSectionSize(int size);

    // -- positions -----------------------------------------------------------
    /// Start of a section in content coordinates (hidden sections are skipped).
    qint64 sectionPosition(int logicalIndex) const;
    /// Start of a section in viewport coordinates.
    int sectionViewportPosition(int logicalIndex) const;
    /// Logical section containing \a contentOffset, or -1.
    int sectionAtOffset(qint64 contentOffset) const;
    /// Visual index of the section containing \a contentOffset, or -1.
    int visualSectionAtOffset(qint64 contentOffset) const;
    /// Sum of the visible section sizes.
    qint64 totalExtent() const;

    // -- order / visibility --------------------------------------------------
    int logicalIndex(int visualIndex) const;
    int visualIndex(int logicalIndex) const;
    void moveSection(int fromVisualIndex, int toVisualIndex);
    /// Follows a QAbstractItemModel column move: section state (size,
    /// visibility) shifts with the columns and the visual order is remapped.
    void moveLogicalSections(int start, int count, int destination);
    /// Remaps sizes after a model row move while keeping identity visual order.
    /// Compact row geometry stays sparse; indexed geometry uses moveLogicalSections().
    void moveLogicalSectionSizes(int start, int count, int destination);
    bool isSectionHidden(int logicalIndex) const;
    void setSectionHidden(int logicalIndex, bool hidden);
    void setAllSectionsHidden(bool hidden);

    // -- horizontal scrolling ------------------------------------------------
    qint64 viewportOffset() const { return m_viewportOffset; }
    void setViewportOffset(qint64 offset);
    qint64 maximumViewportOffset(int viewportExtent) const;

    // -- queries -------------------------------------------------------------
    /// Committed geometry of one section for the current viewport offset.
    ColumnGeometry columnGeometry(int logicalIndex) const;
    /// Visible sections for a viewport extent, as *visual* indices: the range
    /// may contain hidden sections, which callers skip.
    VisibleRange visibleVisualRange(int viewportExtent) const;
    /// Visible visual range of an arbitrary window: content x \a windowStart, extent
    /// \a viewportExtent (both in viewport coordinates, i.e. content minus the offset).
    /// A pane header uses it with its own rect, since a pane that follows the committed
    /// geometry starts at its own x inside the viewport (the table's scrolling pane begins
    /// right of the frozen columns).
    VisibleRange visibleVisualRangeFor(qint64 windowStart, int viewportExtent) const;
    /// Logical indices of the visible sections, in visual order.
    QVector<int> visibleSectionsInVisualOrder() const;
    /// Bumped whenever the visual order of the visible sections may have changed
    /// (move, insert, remove, hide/unhide). A renderer can cache derived state -
    /// e.g. the order it last laid out - and only rebuild it when this number
    /// moved, instead of re-deriving the whole order on every relayout.
    quint32 orderRevision() const { return m_orderRevision; }

    // -- sort state (§33) ----------------------------------------------------
    int sortIndicatorSection() const { return m_sortIndicatorSection; }
    Qt::SortOrder sortIndicatorOrder() const { return m_sortIndicatorOrder; }
    void setSortIndicator(int logicalIndex, Qt::SortOrder order);

    // -- stretch -------------------------------------------------------------
    /// Whether the last visible section absorbs what the other sections leave.
    bool stretchLastSection() const { return m_stretchLastSection; }
    void setStretchLastSection(bool stretch);

    /// Extent the visible sections should fill, in content coordinates: a table hands in
    /// the width its columns have to cover (the viewport, frozen columns included), a
    /// standalone renderer its own axis extent. 0 - the default - means "no target", and
    /// the whole stretch state below stays inert: every section keeps its own size, which
    /// is what a geometry nobody configures for stretching does.
    ///
    /// Deliberately *not* part of saveState(): the extent belongs to the view showing the
    /// sections, not to the column state an application saved, so a restored state is
    /// measured against the view that restores it.
    qint64 stretchExtent() const { return m_stretchExtent; }
    void setStretchExtent(qint64 extent);

    /// Share of the leftover extent: the visible sections with a factor > 0 split
    /// `stretchExtent() - <the sizes the other visible sections keep>` in proportion to
    /// their factors, so 1 : 2 : 1 gives the third section twice the width of the first.
    /// 0 (the default) is a section that keeps its own size.
    ///
    /// The size of a stretching section is *derived*: isSectionSizeExplicit() stays false
    /// for it and a user resize - the header's drag on a section edge, i.e.
    /// resizeSection() - clears the factor, so the section becomes fixed at the dragged
    /// width. That is QHeaderView's "Stretch becomes Interactive" rule, except that the
    /// body follows the geometry here, so both stay the same width.
    qreal sectionStretchFactor(int logicalIndex) const;
    void setSectionStretchFactor(int logicalIndex, qreal factor);
    /// True while at least one section stretches, i.e. while a stretch pass can change
    /// anything (stretchLastSection() counts as one participant).
    bool hasStretchSections() const;

    // -- persistence (§32) ---------------------------------------------------
    static constexpr quint32 kStateMagic = 0x56495648; // 'VIVH'
    /// Version 3 stores compact geometry without expanding every section.
    static constexpr quint32 kStateVersion = 3;
    QByteArray saveState() const;
    /// Restores sizes, order, visibility, sort state and offset. Returns false
    /// (and changes nothing) when the state is invalid or belongs to a different
    /// section count.
    bool restoreState(const QByteArray &state);

signals:
    void sectionResized(int logicalIndex, int oldSize, int newSize);
    void sectionMoved(int logicalIndex, int oldVisualIndex, int newVisualIndex);
    void sectionVisibilityChanged(int logicalIndex, bool visible);
    void sectionCountChanged(int count);
    void sortIndicatorChanged(int logicalIndex, Qt::SortOrder order);
    void stretchLastSectionChanged(bool stretch);
    /// The stretch factor of \a logicalIndex changed (a resize that clears one is
    /// reported through the size signals instead, not as a factor change).
    void sectionStretchFactorChanged(int logicalIndex, qreal factor);
    /// The viewport offset changed; headers only have to shift, no per-section
    /// work is required (keeps scrolling O(1) instead of O(sections)).
    void offsetChanged(qint64 offset);
    /// Emitted once per change, after the granular signals.
    void geometryChanged();
    /// Emitted when the change is *not* described by a granular signal: the size range, the
    /// default size, the stretch flag, a restored state, or a remap that renames many
    /// sections at once (a model-side move). A renderer that applies sectionResized /
    /// sectionVisibilityChanged / offsetChanged / sortIndicatorChanged itself only has to
    /// re-read the whole geometry on this signal - and thus does not pay an O(sections) pass
    /// for a single column resize (P1-10 of the second review).
    void bulkGeometryChanged();

private:
    struct Section
    {
        int size = 0;
        bool hidden = false;
        bool explicitSize = false;
        /// Share of the leftover extent; 0 = the section keeps its own size.
        qreal stretchFactor = 0.0;
    };

    /// Recomputes the sizes of the stretching sections from m_stretchExtent and returns
    /// true when at least one size changed (the caches are invalidated and the size
    /// signals emitted here; the caller still reports its own geometry change). A no-op
    /// while no extent is set or no section is a participant.
    bool applyStretch();
    void rebuildCaches() const;
    /// Rebuilds logicalIndex -> visualIndex from the visual order.
    void rebuildIndexMaps();
    /// Gives the stored section state of a uniform geometry its O(count) representation
    /// (default size, visible, identity order) so a per-section edit has somewhere to go.
    void densify();
    /// Index of \a logicalIndex in the sparse override list, or -1.
    int sparseIndexOf(int logicalIndex) const;
    /// Adds/updates/removes the size override of \a logicalIndex (a size equal to the
    /// default removes it, unless a stretch factor keeps the entry alive) and rebuilds the
    /// delta prefix. O(k log k) with k overrides.
    void setSparseSize(int logicalIndex, int size);
    /// Adds/updates/removes the stretch factor of \a logicalIndex (like setSparseSize(),
    /// an entry survives as long as either its size or its factor is exceptional).
    void setSparseFactor(int logicalIndex, qreal factor);
    /// Slot of a sparse entry, inserting one at the default size / without a factor when
    /// the logical index has none yet. Used by the stretch pass, which writes sizes.
    int sparseSlotFor(int logicalIndex);
    /// Drops sparse entries that are neither a size override nor a stretch factor.
    void pruneSparseList();
    /// Sum of the size deltas of every override *before* \a logicalIndex.
    qint64 sparseDeltaBefore(int logicalIndex) const;
    /// Rebuilds the delta prefix of the override list (positions and the extent).
    void rebuildSparsePrefix();
    /// Clamps every section (and the default size) into the current minimum /
    /// maximum range in one pass, emitting at most one geometryChanged().
    void clampSectionsToTheSizeRange();
    void invalidateCaches();
    void rebuildSpacingPrefix();
    qint64 spacingDeltaBefore(int logicalIndex) const;
    void clearSpacingOverrides();
    void emitGeometryChanged();
    bool isValidVisualOrder(const QVector<qint32> &order) const;
    bool isValidLogical(int logicalIndex) const;
    bool isValidVisual(int visualIndex) const;
    int clampedSize(int size) const;
    const Section *sectionAt(int logicalIndex) const;

    Qt::Orientation m_orientation = Qt::Horizontal;
    QVector<Section> m_sections;    ///< indexed by logical index
    /// Sections of a *uniform* geometry: the count, with every section implied to be
    /// default-sized, visible and in logical order - no per-section storage at all.
    /// 0 means "not uniform" (m_sections is authoritative).
    int m_uniformCount = 0;
    /// Size overrides of a uniform geometry (sorted by logical index): "ten million rows,
    /// one of them 48 px" is k overrides, not ten million Sections - which is what makes
    /// the row-boundary drag usable at that scale (§5/§12 of the vertical-header
    /// decision). An override never changes the order or the visibility: those still
    /// materialise the indexed representation.
    QVector<int> m_sparseKeys;
    QVector<int> m_sparseSizes;
    /// Stretch factors of the same entries (0 = the entry is only a size override).
    QVector<qreal> m_sparseFactors;
    /// Delta prefix of the overrides: entry j is the sum of the deltas of the keys before
    /// key j, so the position of key j is `key*default + m_sparseDeltaPrefix[j]` and the
    /// last entry is the total delta of the geometry.
    QVector<qint64> m_sparseDeltaPrefix;
    QVector<int> m_visualToLogical; ///< visual index -> logical index
    QVector<int> m_logicalToVisual; ///< logical index -> visual index

    int m_defaultSectionSize = 100;
    int m_sectionSpacing = 0;
    QVector<int> m_sectionSpacingOverrides;
    QVector<QPair<int, int>> m_sparseSpacingOverrides;
    QVector<qint64> m_spacingDeltaPrefix;
    int m_minimumSectionSize = 24;
    int m_maximumSectionSize = 100000;
    qint64 m_viewportOffset = 0;
    int m_sortIndicatorSection = -1;
    Qt::SortOrder m_sortIndicatorOrder = Qt::AscendingOrder;
    bool m_stretchLastSection = false;
    /// Extent the visible sections should fill; 0 = no stretch target (see setStretchExtent()).
    qint64 m_stretchExtent = 0;
    /// See orderRevision().
    quint32 m_orderRevision = 1;

    // Lazily rebuilt caches.
    mutable bool m_cacheDirty = true;
    mutable QVector<qint64> m_contentXByLogical;
    mutable QVector<int> m_visibleLogicalOrder;
    mutable qint64 m_totalExtent = 0;
};

} // namespace viv
