#pragma once

#include <QByteArray>
#include <QObject>
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
/// The table body, the native header adapter and (later) a QWidget based header
/// all consume this object; none of them keeps an authoritative width, order or
/// visibility copy of its own (§45.10).
class VIRTUALITEMVIEWS_EXPORT HeaderGeometry : public QObject
{
    Q_OBJECT

public:
    explicit HeaderGeometry(Qt::Orientation orientation = Qt::Horizontal,
                            QObject *parent = nullptr);
    ~HeaderGeometry() override;

    Qt::Orientation orientation() const { return m_orientation; }

    // -- section set ---------------------------------------------------------
    int sectionCount() const { return int(m_sections.size()); }
    /// Grows/shrinks the section set at the end. Model structure changes should
    /// use the logical insert/remove/move calls below instead: they keep the
    /// per-section state (width, visibility, explicit size) with the item it
    /// describes, which appending can never do.
    void setSectionCount(int count);
    /// `columnsInserted(parent, first, last)`: the new sections carry the default
    /// state and appear at the end of the visual order (nothing has moved them
    /// yet), while every section after \a first keeps its own state.
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

    int defaultSectionSize() const { return m_defaultSectionSize; }
    void setDefaultSectionSize(int size);
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
    /// Logical indices of the visible sections, in visual order.
    QVector<int> visibleSectionsInVisualOrder() const;

    // -- sort state (§33) ----------------------------------------------------
    int sortIndicatorSection() const { return m_sortIndicatorSection; }
    Qt::SortOrder sortIndicatorOrder() const { return m_sortIndicatorOrder; }
    void setSortIndicator(int logicalIndex, Qt::SortOrder order);

    // -- stretch -------------------------------------------------------------
    bool stretchLastSection() const { return m_stretchLastSection; }
    void setStretchLastSection(bool stretch);

    // -- persistence (§32) ---------------------------------------------------
    static constexpr quint32 kStateMagic = 0x56495648; // 'VIVH'
    static constexpr quint32 kStateVersion = 1;
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
    /// The viewport offset changed; headers only have to shift, no per-section
    /// work is required (keeps scrolling O(1) instead of O(sections)).
    void offsetChanged(qint64 offset);
    /// Emitted once per change, after the granular signals.
    void geometryChanged();

private:
    struct Section
    {
        int size = 0;
        bool hidden = false;
        bool explicitSize = false;
    };

    void rebuildCaches() const;
    /// Rebuilds logicalIndex -> visualIndex from the visual order.
    void rebuildIndexMaps();
    /// Clamps every section (and the default size) into the current minimum /
    /// maximum range in one pass, emitting at most one geometryChanged().
    void clampSectionsToTheSizeRange();
    void invalidateCaches();
    void emitGeometryChanged();
    bool isValidVisualOrder(const QVector<qint32> &order) const;
    bool isValidLogical(int logicalIndex) const;
    bool isValidVisual(int visualIndex) const;
    int clampedSize(int size) const;
    const Section *sectionAt(int logicalIndex) const;

    Qt::Orientation m_orientation = Qt::Horizontal;
    QVector<Section> m_sections;    ///< indexed by logical index
    QVector<int> m_visualToLogical; ///< visual index -> logical index
    QVector<int> m_logicalToVisual; ///< logical index -> visual index

    int m_defaultSectionSize = 100;
    int m_minimumSectionSize = 24;
    int m_maximumSectionSize = 100000;
    qint64 m_viewportOffset = 0;
    int m_sortIndicatorSection = -1;
    Qt::SortOrder m_sortIndicatorOrder = Qt::AscendingOrder;
    bool m_stretchLastSection = false;

    // Lazily rebuilt caches.
    mutable bool m_cacheDirty = true;
    mutable QVector<qint64> m_contentXByLogical;
    mutable QVector<int> m_visibleLogicalOrder;
    mutable qint64 m_totalExtent = 0;
};

} // namespace viv
