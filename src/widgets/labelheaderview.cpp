#include <virtualitemviews/labelheaderview.h>

#include <virtualitemviews/headergeometry.h>

#include <QAbstractItemModel>
#include <QEvent>
#include <QPainter>
#include <QStyleOptionHeader>

namespace viv {

// ---------------------------------------------------------------------------
// LabelHeaderSection
// ---------------------------------------------------------------------------

LabelHeaderSection::LabelHeaderSection(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // The header reads the pointer through its own filters, so a section must stay
    // passive: taking focus or accepting the press would swallow the resize / drag
    // gesture before VirtualHeaderView sees it (§25).
    setFocusPolicy(Qt::NoFocus);
}

void LabelHeaderSection::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    update();
}

void LabelHeaderSection::setSortOrder(int order)
{
    if (m_sortOrder == order)
        return;
    m_sortOrder = order;
    update();
}

void LabelHeaderSection::setLogicalIndex(int logicalIndex)
{
    if (m_logicalIndex == logicalIndex)
        return;
    m_logicalIndex = logicalIndex;
    update();
}

void LabelHeaderSection::paintEvent(QPaintEvent *)
{
    // The same section widget renders both axes (the column header and the row-number
    // strip) and both are painted with the *horizontal* option, so a strip looks exactly
    // like the column header's sections (panel and gradient). Both edges are
    // finished with the table's separator color after the style paints them.
    Qt::Orientation orientation = Qt::Horizontal;
    const auto *header = qobject_cast<const VirtualHeaderView *>(parentWidget());
    if (header)
        orientation = header->orientation();
    const bool vertical = orientation == Qt::Vertical;

    QStyleOptionHeader option;
    option.initFrom(this);
    option.rect = rect();
    option.text = m_text;
    option.textAlignment = Qt::AlignCenter;
    option.orientation = Qt::Horizontal;
    option.state |= QStyle::State_Horizontal | QStyle::State_Raised;
    // Where the section sits in the *visible* run decides how the style frames it
    // (rounded / flush outer edges). The widget is a child of the header and its
    // geometry is the column/row rect, so its own x (or y) answers that without knowing
    // the geometry's window (§23: the section is where it is drawn, which may be a visual
    // position).
    const QWidget *container = parentWidget();
    const int pos = vertical ? y() : x();
    const int size = vertical ? height() : width();
    const int containerExtent = vertical ? (container ? container->height() : height())
                                         : (container ? container->width() : width());
    const bool first = pos <= 0;
    const bool last = pos + size >= containerExtent;
    option.position = first && last ? QStyleOptionHeader::OnlyOneSection
                                    : (first ? QStyleOptionHeader::Beginning
                                             : (last ? QStyleOptionHeader::End
                                                     : QStyleOptionHeader::Middle));
    option.sortIndicator = m_sortOrder < 0
        ? QStyleOptionHeader::None
        : (m_sortOrder == Qt::AscendingOrder ? QStyleOptionHeader::SortDown
                                             : QStyleOptionHeader::SortUp);
    QPainter painter(this);
    painter.setClipRect(rect().adjusted(0, 0, -2, -2));
    style()->drawControl(QStyle::CE_Header, &option, &painter, this);
    painter.setClipping(false);
    painter.fillRect(QRect(width() - 2, 0, 2, height()), palette().brush(QPalette::Button));
    painter.fillRect(QRect(0, height() - 2, width(), 2), palette().brush(QPalette::Button));
    const bool hasGap = header && header->geometryModel()
        && header->geometryModel()->sectionSpacingAfter(m_logicalIndex) > 0;
    const QColor sectionColor = header && header->sectionSeparatorColor().isValid()
        ? header->sectionSeparatorColor() : headerSectionSeparatorColor(this);
    const QColor crossColor = header && header->crossAxisSeparatorColor().isValid()
        ? header->crossAxisSeparatorColor() : headerSectionSeparatorColor(this);
    const int sectionWidth = header ? header->sectionSeparatorWidth() : 1;
    const int crossWidth = header ? header->crossAxisSeparatorWidth() : 1;
    if (!header || header->crossAxisSeparatorVisible())
        painter.fillRect(vertical ? QRect(width() - crossWidth, 0, crossWidth, height())
                                  : QRect(0, height() - crossWidth, width(), crossWidth), crossColor);
    if ((!header || header->sectionSeparatorsVisible()) && (!vertical || !hasGap))
        painter.fillRect(vertical ? QRect(0, height() - sectionWidth, width(), sectionWidth)
                                  : QRect(width() - sectionWidth, 0, sectionWidth, height()), sectionColor);
}

void LabelHeaderSection::changeEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::FontChange:
    case QEvent::EnabledChange:
        update();
        break;
    default:
        break;
    }
    QWidget::changeEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void LabelHeaderSection::enterEvent(QEnterEvent *event)
#else
void LabelHeaderSection::enterEvent(QEvent *event)
#endif
{
    update();
    QWidget::enterEvent(event);
}

void LabelHeaderSection::leaveEvent(QEvent *event)
{
    update();
    QWidget::leaveEvent(event);
}

// ---------------------------------------------------------------------------
// LabelHeaderAdapter
// ---------------------------------------------------------------------------

QWidget *LabelHeaderAdapter::createSection(WidgetType type, QWidget *parent)
{
    Q_UNUSED(type);
    return new LabelHeaderSection(parent);
}

void LabelHeaderAdapter::setLabelModel(QAbstractItemModel *model)
{
    m_model = model;
}

void LabelHeaderAdapter::setGeometryModel(HeaderGeometry *geometry)
{
    m_geometry = geometry;
}

void LabelHeaderAdapter::bindSection(QWidget *widget, int logicalIndex)
{
    auto *section = static_cast<LabelHeaderSection *>(widget);
    // The header this section belongs to decides the axis: the same adapter can
    // label a horizontal header and the row-number strip.
    Qt::Orientation orientation = Qt::Horizontal;
    if (auto *header = qobject_cast<const VirtualHeaderView *>(section->parentWidget()))
        orientation = header->orientation();
    section->setText(labelText(logicalIndex, orientation));
    section->setSortOrder(sortOrderFor(logicalIndex));
    section->setLogicalIndex(logicalIndex);
}

void LabelHeaderAdapter::unbindSection(QWidget *widget, int logicalIndex)
{
    Q_UNUSED(logicalIndex);
    // A pooled section keeps no identity (§19): drop what the last binding put in.
    auto *section = static_cast<LabelHeaderSection *>(widget);
    section->setText(QString());
    section->setSortOrder(-1);
    section->setLogicalIndex(-1);
}

QString LabelHeaderAdapter::labelText(int logicalIndex, Qt::Orientation orientation) const
{
    if (!m_model)
        return QString();
    return m_model->headerData(logicalIndex, orientation).toString();
}

int LabelHeaderAdapter::sortOrderFor(int logicalIndex) const
{
    if (!m_geometry)
        return -1;
    if (m_geometry->sortIndicatorSection() != logicalIndex)
        return -1;
    return int(m_geometry->sortIndicatorOrder());
}

// ---------------------------------------------------------------------------
// LabelHeaderView
// ---------------------------------------------------------------------------

LabelHeaderView::LabelHeaderView(Qt::Orientation orientation, QWidget *parent)
    : VirtualHeaderView(orientation, parent)
{
    // Handed over: the header unbinds and deletes it with itself.
    setAdapter(new LabelHeaderAdapter, /*takeOwnership=*/true);
}

LabelHeaderView::~LabelHeaderView() = default;

} // namespace viv
