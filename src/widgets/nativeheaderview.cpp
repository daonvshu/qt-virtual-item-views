#include <virtualitemviews/nativeheaderview.h>

#include <virtualitemviews/headergeometry.h>

#include <QPainter>
#include <QApplication>
#include <QImage>
#include <QStyleOptionHeader>

#include <limits>

namespace viv {

namespace {
/// True when \a logicalIndex is part of the pane of an active filter.
bool filterContains(const QVector<int> &filter, bool active, int logicalIndex)
{
    return !active || filter.contains(logicalIndex);
}
} // namespace

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
                setViewportOffset(int(offset));
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
    const bool hidden = m_geometry->isSectionHidden(logicalIndex)
        || !filterContains(m_paneFilter, m_paneFilterActive, logicalIndex);
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
    if (m_paneFilterActive)
        return; // a pane follows the committed visual order, not a local drag
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

    // QHeaderView keeps its section positions in (checked) int arithmetic, so a
    // content extent beyond the int range cannot be mirrored into it at all: touch
    // nothing and say so once instead of letting Qt abort on the overflow (it would
    // also trip on setDefaultSectionSize() alone). The geometry, the pane layout and
    // the compressed scroll bar stay 64-bit, which is what the rest of the view uses.
    if (m_geometry->totalExtent() > qint64(std::numeric_limits<int>::max())) {
        if (!m_extentOverflowWarned) {
            m_extentOverflowWarned = true;
            qWarning("NativeHeaderView: the content extent (%lld px) does not fit into QHeaderView's "
                     "int range; the header keeps its own default sizes",
                     qint64(m_geometry->totalExtent()));
        }
        return;
    }
    m_extentOverflowWarned = false;

    m_applyingToHeader = true;

    const int count = m_geometry->sectionCount();
    setStretchLastSection(m_geometry->stretchLastSection());
    setMinimumSectionSize(m_geometry->minimumSectionSize());
    setMaximumSectionSize(m_geometry->maximumSectionSize());
    setDefaultSectionSize(m_geometry->defaultSectionSize());
    setSortIndicatorShown(m_geometry->sortIndicatorSection() >= 0);
    setSortIndicator(m_geometry->sortIndicatorSection(), m_geometry->sortIndicatorOrder());
    setOffset(int(qBound<qint64>(qint64(0), effectivePaneOffset(),
                                qint64(std::numeric_limits<int>::max()))));

    // Apply only the differences: comparing is cheap, writing invalidates
    // QHeaderView's internal caches.
    int modelSections = count;
    if (model()) {
        modelSections = orientation() == Qt::Horizontal ? model()->columnCount()
                                                       : model()->rowCount();
    }
    // A geometry with fewer sections than the model means "the remaining sections keep the
    // default size" - the uniform-row-height case, where the row geometry deliberately
    // stays empty to save memory. Those sections are mirrored as well: QHeaderView only
    // applies a new default size to sections it creates later, so a strip whose sections
    // were built before the default was known would otherwise keep Qt's own default (and
    // the row numbers would drift away from their rows).
    const int sections = qMin(qMax(count, modelSections), modelSections);
    for (int logical = 0; logical < sections; ++logical) {
        const bool inGeometry = logical < count;
        const bool hidden = inGeometry
            && (m_geometry->isSectionHidden(logical)
                || !filterContains(m_paneFilter, m_paneFilterActive, logical));
        if (isSectionHidden(logical) != hidden)
            setSectionHidden(logical, hidden);
        const int size = inGeometry ? m_geometry->storedSectionSize(logical)
                                    : m_geometry->defaultSectionSize();
        if (!hidden && sectionSize(logical) != size)
            resizeSection(logical, size);
    }
    applyVisualOrder();

    m_applyingToHeader = false;
}

void NativeHeaderView::applyVisualOrder()
{
    // Only the horizontal header mirrors the geometry's visual order; the
    // vertical header always follows the model's row order. This runs inside
    // syncHeaderFromGeometry(), which already owns m_applyingToHeader.
    if (!m_geometry || orientation() != Qt::Horizontal)
        return;

    const int count = m_geometry->sectionCount();
    const int modelSections = model() ? model()->columnCount() : count;
    const int sections = qMin(count, modelSections);
    for (int visual = 0; visual < sections; ++visual) {
        const int logical = m_geometry->logicalIndex(visual);
        if (logical < 0)
            continue;
        const int current = QHeaderView::visualIndex(logical);
        // Comparing before writing keeps QHeaderView's caches valid and makes a
        // user drag (which already moved the section) a no-op here.
        if (current < 0 || current == visual)
            continue;
        moveSection(current, visual);
    }
}

void NativeHeaderView::setViewportOffset(int offset)
{
    if (m_applyingToHeader)
        return;
    if (m_paneOffset != kFollowGeometryOffset)
        return; // the pane owns its offset (setPaneOffset()); a frozen pane never scrolls
    setOffset(qMax(0, offset));
}

qint64 NativeHeaderView::effectivePaneOffset() const
{
    if (!m_geometry)
        return 0;
    if (m_paneOffset != kFollowGeometryOffset)
        return m_paneOffset; // an explicit offset wins, filter or not
    if (m_paneFilterActive) {
        if (m_frozenPane)
            return 0; // a frozen pane never scrolls
    }
    return m_geometry->viewportOffset();
}

void NativeHeaderView::setPaneOffset(qint64 offset)
{
    if (m_paneOffset == offset)
        return;
    m_paneOffset = offset;
    syncHeaderFromGeometry();
}

void NativeHeaderView::setPaneFilter(const QVector<int> &logicalColumns, bool frozen)
{
    if (m_paneFilterActive && m_paneFilter == logicalColumns && m_frozenPane == frozen)
        return;

    m_paneFilter = logicalColumns;
    m_paneFilterActive = true;
    m_frozenPane = frozen;
    // A pane mirrors the committed visual order; local moves would fight it.
    setSectionsMovable(false);
    syncHeaderFromGeometry();
}

void NativeHeaderView::clearPaneFilter()
{
    if (!m_paneFilterActive)
        return;
    m_paneFilterActive = false;
    m_paneFilter.clear();
    m_frozenPane = false;
    m_paneOffset = kFollowGeometryOffset;
    setSectionsMovable(orientation() == Qt::Horizontal);
    syncHeaderFromGeometry();
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

QColor NativeHeaderView::sectionSeparatorColor(const QWidget *context)
{
    // Render a small section with the current style and read the pixel of its
    // right edge: that is exactly the separator between two sections.
    static constexpr int kProbeWidth = 8;
    static constexpr int kProbeHeight = 8;
    QImage probe(kProbeWidth, kProbeHeight, QImage::Format_ARGB32_Premultiplied);
    probe.fill(Qt::transparent);
    {
        QPainter painter(&probe);
        painter.setRenderHint(QPainter::Antialiasing, false);
        QStyleOptionHeader option;
        if (context)
            option.initFrom(context);
        option.state |= QStyle::State_Horizontal | QStyle::State_Enabled;
        option.orientation = Qt::Horizontal;
        option.position = QStyleOptionHeader::Middle; // draws the separator
        option.rect = QRect(0, 0, kProbeWidth, kProbeHeight);
        const QWidget *styleSource = context;
        const_cast<QStyle *>(styleSource ? styleSource->style() : QApplication::style())
            ->drawControl(QStyle::CE_HeaderSection, &option, &painter,
                          const_cast<QWidget *>(styleSource));
    }

    const QColor separator = probe.pixelColor(kProbeWidth - 1, kProbeHeight / 2);
    if (separator.isValid() && separator.alpha() > 0)
        return separator;
    // Fall back to a palette role when the style draws nothing there.
    return context ? context->palette().color(QPalette::Mid) : QColor(160, 160, 160);
}

void NativeHeaderView::drawPaneSeparator(QPainter *painter, const QRect &rect,
                                        const PaneSeparatorStyle &style,
                                        const QColor &styleSeparatorColor)
{
    if (!painter || rect.isEmpty() || !style.isVisible())
        return;
    const QColor color = style.effectiveColor(styleSeparatorColor);
    if (!color.isValid())
        return;
    if (style.lineStyle == Qt::SolidLine) {
        painter->fillRect(rect, color);
        return;
    }
    QPen pen(color, qMax(1, style.width), style.lineStyle);
    painter->save();
    painter->setPen(pen);
    const int x = rect.center().x();
    painter->drawLine(x, rect.top(), x, rect.bottom());
    painter->restore();
}

} // namespace viv
