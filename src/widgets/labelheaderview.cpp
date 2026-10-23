#include <virtualitemviews/labelheaderview.h>

#include <virtualitemviews/headergeometry.h>

#include <QAbstractItemModel>
#include <QPainter>
#include <QStyleOptionHeader>
#include <QVariant>

namespace viv {

// ---------------------------------------------------------------------------
// LabelHeaderSection
// ---------------------------------------------------------------------------

LabelHeaderSection::LabelHeaderSection(QWidget *parent)
    : StyledHeaderSection(parent)
{
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

void LabelHeaderSection::initStyleOption(QStyleOptionHeader *option) const
{
    StyledHeaderSection::initStyleOption(option);
    option->text = m_text;
    const auto *header = headerView();
    option->textAlignment = m_textAlignment ? m_textAlignment
        : (header ? header->textAlignment() : Qt::Alignment(Qt::AlignCenter));
    if (m_foregroundBrush.style() != Qt::NoBrush)
        option->palette.setBrush(QPalette::ButtonText, m_foregroundBrush);
    else if (header && header->foregroundColor().isValid())
        option->palette.setColor(QPalette::ButtonText, header->foregroundColor());
    option->sortIndicator = m_sortOrder < 0
        ? QStyleOptionHeader::None
        : (m_sortOrder == Qt::AscendingOrder ? QStyleOptionHeader::SortDown
                                             : QStyleOptionHeader::SortUp);
}

void LabelHeaderSection::setTextAlignment(Qt::Alignment alignment)
{
    if (m_textAlignment == alignment)
        return;
    m_textAlignment = alignment;
    update();
}

void LabelHeaderSection::setForegroundBrush(const QBrush &brush)
{
    if (m_foregroundBrush == brush)
        return;
    m_foregroundBrush = brush;
    update();
}

void LabelHeaderSection::initContentStyleOption(QStyleOptionHeader *option) const
{
    initStyleOption(option);
    if (const auto *header = headerView()) {
        const QMargins padding = header->contentPadding();
        if (qint64(padding.left()) + padding.right() >= option->rect.width()
            || qint64(padding.top()) + padding.bottom() >= option->rect.height()) {
            option->rect = QRect();
            return;
        }
        option->rect.adjust(padding.left(), padding.top(), -padding.right(), -padding.bottom());
    }
}

void LabelHeaderSection::paintContent(QPainter *painter)
{
    QStyleOptionHeader option;
    initContentStyleOption(&option);
    if (!option.rect.isValid())
        return;
    // Padding only constrains content; the panel and separators retain the section bounds.
    painter->save();
    painter->setClipRect(option.rect, Qt::IntersectClip);
    QStyleOptionHeader labelOption = option;
    labelOption.rect = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);
    if (labelOption.rect.isValid())
        style()->drawControl(QStyle::CE_HeaderLabel, &labelOption, painter, this);
    if (option.sortIndicator != QStyleOptionHeader::None) {
        QStyleOptionHeader arrowOption = option;
        arrowOption.rect = style()->subElementRect(QStyle::SE_HeaderArrow, &option, this);
        if (arrowOption.rect.isValid())
            style()->drawPrimitive(QStyle::PE_IndicatorHeaderArrow, &arrowOption, painter, this);
    }
    painter->restore();
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
    // The section's owner decides the axis for both column labels and row numbers.
    section->setText(labelText(logicalIndex, section->orientation()));
    section->setSortOrder(sortOrderFor(logicalIndex));
    const Qt::Orientation axis = section->orientation();
    const QVariant alignment = m_model
        ? m_model->headerData(logicalIndex, axis, Qt::TextAlignmentRole) : QVariant();
    const QVariant foreground = m_model
        ? m_model->headerData(logicalIndex, axis, Qt::ForegroundRole) : QVariant();
    const QVariant font = m_model
        ? m_model->headerData(logicalIndex, axis, Qt::FontRole) : QVariant();
    section->setTextAlignment(alignment.isValid()
        ? Qt::Alignment(alignment.toInt()) : Qt::Alignment());
    section->setForegroundBrush(foreground.canConvert<QBrush>()
        ? qvariant_cast<QBrush>(foreground) : QBrush());
    // Unresolved properties inherit from the header, including after a model role is cleared.
    section->setFont(font.canConvert<QFont>() ? qvariant_cast<QFont>(font) : QFont());
    HeaderSectionAdapter::bindSection(widget, logicalIndex);
}

void LabelHeaderAdapter::unbindSection(QWidget *widget, int logicalIndex)
{
    // A pooled section keeps no identity (§19): drop what the last binding put in.
    auto *section = static_cast<LabelHeaderSection *>(widget);
    section->setText(QString());
    section->setSortOrder(-1);
    section->setTextAlignment(Qt::Alignment());
    section->setForegroundBrush(QBrush());
    section->setFont(QFont());
    HeaderSectionAdapter::unbindSection(widget, logicalIndex);
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

} // namespace viv
