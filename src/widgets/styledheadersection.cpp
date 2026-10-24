#include <virtualitemviews/styledheadersection.h>
#include "../core/pixelalignedlines_p.h"

#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/virtualheaderview.h>
#include <virtualitemviews/virtualtableview.h>

#include <QPainter>
#include <QStyleOptionHeader>

namespace viv {

namespace {
// The last row has no trailing gap, even when the geometry stores configured spacing.
bool hasTrailingSectionGap(const VirtualHeaderView *header, int logicalIndex)
{
    const HeaderGeometry *geometry = header ? header->geometryModel() : nullptr;
    if (!geometry || geometry->sectionSpacingAfter(logicalIndex) <= 0)
        return false;
    return header->orientation() != Qt::Vertical
        || geometry->sectionPosition(logicalIndex) + geometry->sectionSize(logicalIndex)
            < geometry->totalExtent();
}
} // namespace

StyledHeaderSection::StyledHeaderSection(QWidget *parent)
    : HeaderSection(parent)
{
}

void StyledHeaderSection::initStyleOption(QStyleOptionHeader *option) const
{
    option->initFrom(this);
    const auto *table = tableView();
    if (headerView() && table && table->headerVisualStateBackgroundVisible(orientation())) {
        // Native interactive highlights are immediate rectangles beneath the animated layer.
        option->state &= ~(QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_On
                           | QStyle::State_Selected | QStyle::State_HasFocus);
    }
    option->rect = rect();
    option->textAlignment = Qt::AlignCenter;
    // Render both axes with the same native panel style.
    option->orientation = Qt::Horizontal;
    option->state |= QStyle::State_Horizontal | QStyle::State_Raised;
    const bool vertical = orientation() == Qt::Vertical;
    const QWidget *container = parentWidget();
    const int pos = vertical ? y() : x();
    const int size = vertical ? height() : width();
    const int extent = vertical ? (container ? container->height() : height())
                                : (container ? container->width() : width());
    const bool first = pos <= 0;
    const bool last = pos + size >= extent;
    option->position = first && last ? QStyleOptionHeader::OnlyOneSection
                                    : (first ? QStyleOptionHeader::Beginning
                                             : (last ? QStyleOptionHeader::End
                                                     : QStyleOptionHeader::Middle));
}

QBrush StyledHeaderSection::visualStateBackgroundBrush(bool selected, const QColor &defaultColor) const
{
    Q_UNUSED(selected);
    return QBrush(defaultColor);
}

void StyledHeaderSection::paintBackground(QPainter *painter)
{
    const auto *header = headerView();
    const auto *table = tableView();
    const bool backgroundVisible = header ? header->backgroundVisible()
        : (!table || table->headerBackgroundVisible());
    const QColor backgroundColor = header ? header->backgroundColor()
        : (table ? table->headerBackgroundColor() : QColor());
    if (backgroundVisible) {
        if (backgroundColor.isValid()) {
            // The header container already fills sections; avoid compositing alpha twice.
            // The corner has no header container and needs its own base fill.
            if (!header)
                painter->fillRect(rect(), backgroundColor);
        } else {
            QStyleOptionHeader option;
            initStyleOption(&option);
            // Keep native trailing borders outside the widget so custom separators reach its edges.
            option.rect.adjust(0, 0, 2, 2);
            style()->drawControl(QStyle::CE_HeaderSection, &option, painter, this);
        }
    }

    if (!header || !table || !table->headerVisualStateBackgroundVisible(orientation()))
        return;
    const auto state = visualState();
    const int radius = table->visualStateCornerRadiusIncludesHeaders()
        ? table->visualStateCornerRadius() : 0;
    // Only row selection joins the row-header background to the body.
    const bool roundRight = orientation() != Qt::Vertical
        || table->selectionBehavior() == VirtualItemView::SelectionBehavior::SelectItems;
    painter->save();
    if (orientation() == Qt::Vertical && header->sectionSeparatorsVisible()
        && !hasTrailingSectionGap(header, logicalIndex()))
        clipPixelAlignedBottomInset(painter, rect(), header->sectionSeparatorWidth());
    painter->setOpacity(state.hoverProgress);
    fillPixelAlignedRoundedRegion(painter, rect(), QRegion(rect()), radius,
                                  visualStateBackgroundBrush(false, table->hoverBackgroundColor()),
                                  true, roundRight);
    painter->setOpacity(state.selectedProgress);
    fillPixelAlignedRoundedRegion(painter, rect(), QRegion(rect()), radius,
                                  visualStateBackgroundBrush(true, table->selectedBackgroundColor()),
                                  true, roundRight);
    painter->restore();
}

void StyledHeaderSection::paintForeground(QPainter *painter)
{
    const auto *header = headerView();
    const bool vertical = orientation() == Qt::Vertical;
    const bool hasGap = hasTrailingSectionGap(header, logicalIndex());
    const QColor sectionColor = header && header->sectionSeparatorColor().isValid()
        ? header->sectionSeparatorColor() : headerSectionSeparatorColor(this);
    const QColor crossColor = header && header->crossAxisSeparatorColor().isValid()
        ? header->crossAxisSeparatorColor() : headerSectionSeparatorColor(this);
    const int sectionWidth = header ? header->sectionSeparatorWidth() : 1;
    const int crossWidth = header ? header->crossAxisSeparatorWidth() : 1;
    if (!header || header->crossAxisSeparatorVisible()) {
        if (vertical)
            fillVerticalPixelLines(painter, {QRect(width() - crossWidth, 0, crossWidth, height())},
                                   crossWidth, crossColor, true);
        else
            fillHorizontalPixelLines(painter, {QRect(0, height() - crossWidth, width(), crossWidth)},
                                     crossWidth, crossColor, true);
    }
    if ((!header || header->sectionSeparatorsVisible()) && (!vertical || !hasGap)) {
        if (vertical)
            fillHorizontalPixelLines(painter, {QRect(0, height() - sectionWidth, width(), sectionWidth)},
                                     sectionWidth, sectionColor, true);
        else
            fillVerticalPixelLines(painter, {QRect(width() - sectionWidth, 0, sectionWidth, height())},
                                   sectionWidth, sectionColor, true);
    }
}

} // namespace viv
