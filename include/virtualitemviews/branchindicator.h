#pragma once

#include <virtualitemviews/global.h>

#include <QIcon>
#include <QMargins>
#include <QModelIndex>
#include <QRect>
#include <QtGlobal>

class QPainter;
class QPalette;
class QColor;

namespace viv {

/// Geometry of the built-in branch indicator inside its cell.
constexpr int kBranchIndicatorSize = 10;
/// Legacy leading margin for callers that opt into left-aligned geometry; the
/// view's default indicator layout is centered with zero padding.
constexpr int kBranchIndicatorMargin = 4;

/// One indicator cell of a tree row.
///
/// The flags are the QTreeView::branch state vocabulary (has-children,
/// has-siblings, adjoins-item, open/closed) - no style sheet is parsed, so a
/// renderer can match the states it cares about and ignore the rest.
///
/// A row at depth N owns N + 1 cells: cell 0 is the row's own cell and carries
/// the expand/collapse indicator (QTreeView::branch:adjoins-item), cell L > 0
/// belongs to the ancestor at depth L and is where the connector lines of a
/// "├─" style tree are drawn.
struct BranchIndicatorState
{
    /// The item of this cell has children (has-children).
    bool hasChildren = false;
    /// The item of this cell has a sibling below it at the same level
    /// (has-siblings).
    bool hasSiblings = false;
    /// This cell belongs to the row itself (adjoins-item). Only this cell
    /// carries the expand/collapse indicator; the others are ancestors.
    bool adjoinsItem = false;
    /// The item of this cell is expanded; only meaningful with children.
    bool isExpanded = false;
    /// Absolute level of this cell: 0 = a top level item, `itemDepth` = the
    /// row's own cell (the one with the expand/collapse indicator).
    int cellDepth = 0;
    /// Depth of the row this cell was painted for (0 = top level item).
    int itemDepth = 0;

    /// QTreeView::branch:open / :closed.
    bool isOpen() const { return hasChildren && isExpanded; }
    bool isClosed() const { return hasChildren && !isExpanded; }
    /// QTreeView::branch:!has-children.
    bool isLeaf() const { return !hasChildren; }
};

/// Paints the branch indicators of a VirtualTreeView.
///
/// The view offers every cell of every visible row, so a renderer can reproduce
/// the QTreeView look (connector lines plus an arrow/dot) or draw something
/// completely different. A cell rect is the full indentation wide slot of its
/// level, in viewport coordinates.
///
/// Override either hook: branchIcon() for the simple "one icon per state" case
/// (the default paintBranch() centres it in the cell), or paintBranch() when the
/// decoration is not a plain icon.
class VIRTUALITEMVIEWS_EXPORT BranchIndicatorRenderer
{
public:
    virtual ~BranchIndicatorRenderer();

    /// Icon of \a state; the default returns a null icon (paint nothing).
    virtual QIcon branchIcon(const BranchIndicatorState &state, const QModelIndex &index) const;

    /// Square target rect of a branch indicator inside \a cellRect.
    ///
    /// \a padding is removed from the cell in logical pixels before alignment;
    /// negative values are clamped to zero. Missing horizontal or vertical
    /// alignment flags fall back to AlignLeft or AlignVCenter. The size is
    /// clamped to the padded area and an empty rect is returned when no room
    /// remains. Custom renderers can use this to match the built-in geometry.
    static QRect branchIndicatorTargetRect(const QRect &cellRect, int size,
                                           Qt::Alignment alignment,
                                           const QMargins &padding);

    /// Floating-point target in logical pixels, preserving fractional centers.
    /// Use this for painting; the painter applies the device pixel ratio.
    static QRectF branchIndicatorTargetRectF(const QRectF &cellRect, qreal size,
                                             Qt::Alignment alignment,
                                             const QMarginsF &padding);

    /// Paints the cell of \a state into \a cellRect (viewport coordinates).
    virtual void paintBranch(QPainter *painter, const BranchIndicatorState &state,
                             const QModelIndex &index, const QRect &cellRect) const;

    /// The indicator the view draws without a renderer: a rounded equilateral triangle in the cell
    /// that adjoins a row with children, nothing for the other cells. Renderers
    /// can call this for the states they do not customize.
    static void paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                   const QRect &cellRect, const QPalette &palette);
    /// Configurable rounded triangle; size is clamped to the cell, invalid color uses the palette.
    /// The default alignment is centered with zero padding.
    static void paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                   const QRect &cellRect, const QPalette &palette,
                                   int size, const QColor &color);
    /// Same, with explicit alignment and padding inside the cell.
    static void paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                   const QRect &cellRect, const QPalette &palette,
                                   int size, const QColor &color,
                                   Qt::Alignment alignment, const QMargins &padding);
    /// Same, rotating in degrees around the triangle's centroid, which stays at
    /// the aligned icon-frame center throughout the rotation.
    static void paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                   const QRect &cellRect, const QPalette &palette,
                                   int size, const QColor &color,
                                   Qt::Alignment alignment, const QMargins &padding,
                                   qreal rotationDegrees);
    /// Same, using the application palette.
    static void paintBuiltinBranch(QPainter *painter, const BranchIndicatorState &state,
                                   const QRect &cellRect);
};

} // namespace viv
