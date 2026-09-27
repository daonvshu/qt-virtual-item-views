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

void LabelHeaderSection::paintEvent(QPaintEvent *)
{
    // The same section widget renders both axes (the column header and the row-number
    // strip) and both are painted with the *horizontal* option, so a strip looks exactly
    // like the column header's sections (panel, gradient, edges). The axis only decides
    // where the trailing separator goes: the style draws it on the right edge, so a
    // vertical section draws its own line at the bottom with the same colour.
    Qt::Orientation orientation = Qt::Horizontal;
    if (auto *header = qobject_cast<const VirtualHeaderView *>(parentWidget()))
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
    style()->drawControl(QStyle::CE_Header, &option, &painter, this);
    if (vertical) {
        // Same line as between two columns, just along the other axis (see
        // headerSectionSeparatorColor()).
        const QColor separator = headerSectionSeparatorColor(this);
        if (separator.isValid())
            painter.fillRect(rect().left(), rect().bottom(), rect().width(), 1, separator);
    }
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
}

void LabelHeaderAdapter::unbindSection(QWidget *widget, int logicalIndex)
{
    Q_UNUSED(logicalIndex);
    // A pooled section keeps no identity (§19): drop what the last binding put in.
    auto *section = static_cast<LabelHeaderSection *>(widget);
    section->setText(QString());
    section->setSortOrder(-1);
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
    , m_labelAdapter(new LabelHeaderAdapter)
{
    // Handed over: the header unbinds and deletes it with itself.
    setAdapter(m_labelAdapter, /*takeOwnership=*/true);
}

LabelHeaderView::~LabelHeaderView() = default;

} // namespace viv
