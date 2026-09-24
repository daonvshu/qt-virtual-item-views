#include <virtualitemviews/nativeheaderview.h>

#include <virtualitemviews/headergeometry.h>

namespace viv {

NativeHeaderView::NativeHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    setSectionsClickable(true);
    setHighlightSections(false);
    setSectionsMovable(orientation == Qt::Horizontal);
    // Interactive keeps user resizing possible; the geometry decides the sizes,
    // so no stretch/resize-to-contents mode may take over.
    setSectionResizeMode(QHeaderView::Interactive);
    setStretchLastSection(false);
    setDefaultAlignment(orientation == Qt::Horizontal ? Qt::AlignLeft | Qt::AlignVCenter
                                                      : Qt::AlignCenter);
    setSortIndicatorShown(false);

    connect(this, &QHeaderView::sectionResized, this, &NativeHeaderView::onHeaderSectionResized);
    connect(this, &QHeaderView::sectionMoved, this, &NativeHeaderView::onHeaderSectionMoved);
    connect(this, &QHeaderView::sectionClicked, this, [this](int logicalIndex) {
        if (!m_sortInteractionEnabled || !m_geometry || logicalIndex < 0)
            return;
        const bool sameSection = m_geometry->sortIndicatorSection() == logicalIndex;
        const Qt::SortOrder order =
            sameSection && m_geometry->sortIndicatorOrder() == Qt::AscendingOrder
            ? Qt::DescendingOrder
            : Qt::AscendingOrder;
        m_geometry->setSortIndicator(logicalIndex, order);
    });
}

NativeHeaderView::~NativeHeaderView() = default;

void NativeHeaderView::setGeometryModel(HeaderGeometry *geometry)
{
    if (m_geometry == geometry)
        return;
    connectGeometry(m_geometry, false);
    m_geometry = geometry;
    connectGeometry(m_geometry, true);
    syncHeaderFromGeometry();
}

void NativeHeaderView::connectGeometry(HeaderGeometry *geometry, bool connectSignals)
{
    if (!geometry)
        return;
    if (connectSignals) {
        connect(geometry, &HeaderGeometry::geometryChanged, this, &NativeHeaderView::onGeometryChanged);
        // Granular signals carry the section index, so only that section is
        // re-applied (a full sync would be O(sections) per change).
        connect(geometry, &HeaderGeometry::sectionResized, this,
                [this](int logicalIndex, int, int) { applySection(logicalIndex); });
        connect(geometry, &HeaderGeometry::offsetChanged, this, [this](qint64 offset) {
            if (!m_applyingToHeader)
                setOffset(int(qMax<qint64>(0, offset)));
        });
        connect(geometry, &HeaderGeometry::sectionMoved, this, [this](int, int, int) {
            syncHeaderFromGeometry();
        });
        connect(geometry, &HeaderGeometry::sectionVisibilityChanged, this,
                [this](int logicalIndex, bool) { applySection(logicalIndex); });
        connect(geometry, &HeaderGeometry::sectionCountChanged, this, [this](int) {
            syncHeaderFromGeometry();
        });
        connect(geometry, &HeaderGeometry::sortIndicatorChanged, this, [this](int, Qt::SortOrder) {
            syncHeaderFromGeometry();
        });
        connect(geometry, &HeaderGeometry::stretchLastSectionChanged, this, [this](bool) {
            syncHeaderFromGeometry();
        });
    } else {
        disconnect(geometry, nullptr, this, nullptr);
    }
}

void NativeHeaderView::applySection(int logicalIndex)
{
    if (!m_geometry || m_applyingToHeader)
        return;
    if (logicalIndex < 0 || logicalIndex >= m_geometry->sectionCount())
        return;
    const int modelSections = orientation() == Qt::Horizontal
        ? (model() ? model()->columnCount() : m_geometry->sectionCount())
        : (model() ? model()->rowCount() : m_geometry->sectionCount());
    if (logicalIndex >= modelSections)
        return;

    m_applyingToHeader = true;
    const bool hidden = m_geometry->isSectionHidden(logicalIndex);
    if (isSectionHidden(logicalIndex) != hidden)
        setSectionHidden(logicalIndex, hidden);
    const int size = m_geometry->storedSectionSize(logicalIndex);
    if (!hidden && sectionSize(logicalIndex) != size)
        resizeSection(logicalIndex, size);
    m_applyingToHeader = false;
}

void NativeHeaderView::onGeometryChanged()
{
    syncHeaderFromGeometry();
}

void NativeHeaderView::onHeaderSectionResized(int logicalIndex, int oldSize, int newSize)
{
    Q_UNUSED(oldSize);
    if (m_applyingToHeader || !m_geometry)
        return;
    syncGeometryFromHeaderSectionSize(logicalIndex, newSize);
}

void NativeHeaderView::onHeaderSectionMoved(int logicalIndex, int oldVisualIndex, int newVisualIndex)
{
    if (m_applyingToHeader || !m_geometry)
        return;
    syncGeometryFromHeaderMove(logicalIndex, oldVisualIndex, newVisualIndex);
}

void NativeHeaderView::syncGeometryFromHeaderSectionSize(int logicalIndex, int size)
{
    if (!m_geometry || logicalIndex < 0 || logicalIndex >= m_geometry->sectionCount())
        return;
    if (m_geometry->storedSectionSize(logicalIndex) == size)
        return;
    // The user resized the section: the geometry is updated and stays the
    // authority, the body re-queries it.
    m_geometry->resizeSection(logicalIndex, size);
}

void NativeHeaderView::syncGeometryFromHeaderMove(int logicalIndex, int oldVisualIndex, int newVisualIndex)
{
    if (!m_geometry)
        return;
    if (m_geometry->logicalIndex(newVisualIndex) == logicalIndex)
        return; // already applied
    if (m_geometry->visualIndex(logicalIndex) == newVisualIndex)
        return;
    m_geometry->moveSection(oldVisualIndex, newVisualIndex);
}

void NativeHeaderView::syncHeaderFromGeometry()
{
    if (!m_geometry || m_applyingToHeader)
        return;

    m_applyingToHeader = true;

    const int count = m_geometry->sectionCount();
    setStretchLastSection(m_geometry->stretchLastSection());
    setMinimumSectionSize(m_geometry->minimumSectionSize());
    setMaximumSectionSize(m_geometry->maximumSectionSize());
    setDefaultSectionSize(m_geometry->defaultSectionSize());
    setSortIndicatorShown(m_geometry->sortIndicatorSection() >= 0);
    setSortIndicator(m_geometry->sortIndicatorSection(), m_geometry->sortIndicatorOrder());
    setOffset(int(qMax<qint64>(0, m_geometry->viewportOffset())));

    // Apply only the differences: comparing is cheap, writing invalidates
    // QHeaderView's internal caches.
    int modelSections = count;
    if (model()) {
        modelSections = orientation() == Qt::Horizontal ? model()->columnCount()
                                                       : model()->rowCount();
    }
    const int sections = qMin(count, modelSections);
    for (int logical = 0; logical < sections; ++logical) {
        const bool hidden = m_geometry->isSectionHidden(logical);
        if (isSectionHidden(logical) != hidden)
            setSectionHidden(logical, hidden);
        const int size = m_geometry->storedSectionSize(logical);
        if (!hidden && sectionSize(logical) != size)
            resizeSection(logical, size);
    }

    m_applyingToHeader = false;
}

void NativeHeaderView::setViewportOffset(int offset)
{
    if (m_applyingToHeader)
        return;
    setOffset(qMax(0, offset));
}

void NativeHeaderView::setLabelModel(QAbstractItemModel *model)
{
    if (QHeaderView::model() == model)
        return;
    setModel(model);
    syncHeaderFromGeometry();
}

void NativeHeaderView::setSortInteractionEnabled(bool enabled)
{
    m_sortInteractionEnabled = enabled;
}

} // namespace viv
