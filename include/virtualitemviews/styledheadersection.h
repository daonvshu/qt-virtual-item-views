#pragma once

#include <virtualitemviews/headersection.h>

#include <QBrush>

class QStyleOptionHeader;

namespace viv {

/// Header appearance shared by label sections, custom sections and the corner widget.
/// Paints the native panel, animated state backgrounds, rounded corners and separators.
/// Provides no text or other content; section gaps remain VirtualHeaderView's responsibility.
class VIRTUALITEMVIEWS_EXPORT StyledHeaderSection : public HeaderSection
{
    Q_OBJECT

public:
    /// Creates a styled section with no label content.
    explicit StyledHeaderSection(QWidget *parent = nullptr);

protected:
    /// Prepares native panel geometry and neutralizes native interactive backgrounds
    /// when the table owns hover/selection painting. Subclasses may add label content.
    virtual void initStyleOption(QStyleOptionHeader *option) const;
    /// Brush of the hover or selected layer; the default uses the owning table's color.
    /// Override the selected brush to customize its fill while retaining default hover,
    /// corner geometry, animation, labels and separators. Coordinates are section-local.
    virtual QBrush visualStateBackgroundBrush(bool selected, const QColor &defaultColor) const;
    /// Draws the optional base background followed by the table's animated state backgrounds.
    void paintBackground(QPainter *painter) override;
    /// Draws section and cross-axis separators after content, excluding section gaps.
    void paintForeground(QPainter *painter) override;
};

} // namespace viv
