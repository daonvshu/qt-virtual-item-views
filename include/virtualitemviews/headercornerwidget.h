#pragma once

#include <virtualitemviews/styledheadersection.h>

namespace viv {

/// Empty corner between table headers, sharing their panel style without label content.
/// Border colors, widths and visibility come from the parent VirtualTableView's grid.
class VIRTUALITEMVIEWS_EXPORT HeaderCornerWidget : public StyledHeaderSection
{
    Q_OBJECT

public:
    /// Creates a corner whose parent table supplies its grid appearance.
    explicit HeaderCornerWidget(QWidget *parent = nullptr);

protected:
    /// Renders a complete native panel independently of row/column section position.
    void initStyleOption(QStyleOptionHeader *option) const override;
    /// Draws only the table-grid edges that meet the header bands.
    void paintForeground(QPainter *painter) override;
};

} // namespace viv
