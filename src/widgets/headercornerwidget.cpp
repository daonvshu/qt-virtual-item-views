#include <virtualitemviews/headercornerwidget.h>
#include "../core/pixelalignedlines_p.h"

#include <virtualitemviews/virtualtableview.h>

#include <QPainter>
#include <QStyleOptionHeader>

namespace viv {

HeaderCornerWidget::HeaderCornerWidget(QWidget *parent)
    : StyledHeaderSection(parent)
{
}

void HeaderCornerWidget::initStyleOption(QStyleOptionHeader *option) const
{
    StyledHeaderSection::initStyleOption(option);
    option->position = QStyleOptionHeader::OnlyOneSection;
}

void HeaderCornerWidget::paintForeground(QPainter *painter)
{
    const auto *table = tableView();
    if (!table) {
        StyledHeaderSection::paintForeground(painter);
        return;
    }
    const QColor fallback = headerSectionSeparatorColor(table);
    const QColor verticalColor = table->verticalGridLineColor().isValid()
        ? table->verticalGridLineColor() : fallback;
    const QColor horizontalColor = table->horizontalGridLineColor().isValid()
        ? table->horizontalGridLineColor() : fallback;
    const int verticalWidth = table->verticalGridLineWidth();
    const int horizontalWidth = table->horizontalGridLineWidth();
    if (table->horizontalGridLinesVisible())
        fillHorizontalPixelLines(painter,
            {QRect(0, height() - horizontalWidth, width(), horizontalWidth)},
            horizontalWidth, horizontalColor, true);
    if (table->verticalGridLinesVisible())
        fillVerticalPixelLines(painter,
            {QRect(width() - verticalWidth, 0, verticalWidth, height())},
            verticalWidth, verticalColor, true);
}

} // namespace viv
