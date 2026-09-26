#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/branchindicator.h>
#include <virtualitemviews/treevisibilityindex.h>
#include <virtualitemviews/virtualitemview.h>

#include <QPersistentModelIndex>

class QAbstractItemModel;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

namespace viv {

class ListLayout;

/// Tree MVP (architecture document §43 v0.6).
///
/// The tree is a list kernel driven by a TreeVisibilityIndex:
///  - the model tree is flattened into *visible rows* (expanded nodes only),
///  - the layout axis (row heights, scrolling, recycling) is the same kernel as
///    the List and the Table,
///  - each materialized row is one business QWidget, inset by
///    `indentation * depth`; the branch indicator is drawn by the view and is
///    clickable, so the row widget stays untouched.
///
/// Model mutations (insert/remove/move/sort/filter/reset) rebuild the visible
/// row mapping while keeping the expansion state; expand/collapse are
/// incremental inside TreeVisibilityIndex.
class VIRTUALITEMVIEWS_EXPORT VirtualTreeView : public VirtualItemView
{
    Q_OBJECT

public:
    explicit VirtualTreeView(QWidget *parent = nullptr);
    ~VirtualTreeView() override;

    void setModel(QAbstractItemModel *model) override;

    /// Root of the flattened sub-tree; a persistent index, like the list's root.
    void setRootIndex(const QModelIndex &index);
    QModelIndex rootIndex() const { return m_rootIndex; }

    // -- expansion -----------------------------------------------------------
    void expand(const QModelIndex &index);
    void collapse(const QModelIndex &index);
    /// Expands \a index and every branch below it (the `*` key, like
    /// QTreeView::expandRecursively).
    void expandRecursively(const QModelIndex &index);
    void setExpanded(const QModelIndex &index, bool expanded);
    void toggleExpanded(const QModelIndex &index);
    bool isExpanded(const QModelIndex &index) const;
    bool hasChildren(const QModelIndex &index) const;
    void collapseAll();

    // -- branch UI -----------------------------------------------------------
    void setIndentation(int pixels);
    int indentation() const { return m_indentation; }
    /// Draws and handles the expand/collapse indicator of branch items.
    void setBranchIndicatorsVisible(bool visible);
    bool branchIndicatorsVisible() const { return m_branchIndicatorsVisible; }

    // -- branch decoration ---------------------------------------------------
    /// Custom indicator renderer; null (the default) keeps the built-in
    /// triangle. Every cell of every visible row is offered to the renderer, so
    /// it can also draw the ancestor connector cells (QTreeView::branch without
    /// style sheets, see branchindicator.h for the state vocabulary).
    void setBranchIndicatorRenderer(BranchIndicatorRenderer *renderer, bool takeOwnership = false);
    BranchIndicatorRenderer *branchIndicatorRenderer() const { return m_branchRenderer; }

    /// State of one indicator cell on the path of \a index. \a cellDepth is the
    /// absolute level (0 = a top level item) and is clamped to the row's depth;
    /// a negative value (the default) is the row's own cell, the one that
    /// carries the expand/collapse indicator.
    BranchIndicatorState branchState(const QModelIndex &index, int cellDepth = -1) const;

    TreeVisibilityIndex *visibilityIndex() const { return m_visibility; }
    qsizetype visibleRowCount() const;
    /// Depth of an index in the (possibly root-restricted) tree.
    int itemDepth(const QModelIndex &index) const override;

signals:
    void expanded(const QModelIndex &index);
    void collapsed(const QModelIndex &index);

protected:
    qsizetype viewItemCount() const override;
    QModelIndex viewIndex(qsizetype item, int column = 0) const override;
    qsizetype viewItemForIndex(const QModelIndex &index) const override;
    /// A tree never maps model rows onto layout rows directly: model mutations
    /// are handled through TreeVisibilityIndex instead.
    bool isLayoutParent(const QModelIndex &parent) const override;
    QRect geometryForViewRow(qsizetype row) const override;
    bool handleItemKeyPress(QKeyEvent *event) override;
    /// Repaints the branch indicator strip: the indicators are painted by the
    /// viewport, so they are not moved by the row widgets and must be
    /// invalidated after every materialization pass (scroll, expand/collapse,
    /// indentation, resize).
    void afterMaterialize() override;
    /// §38: a tree drops *between* siblings or *into* an item. The middle band
    /// of a drop-enabled item expresses "become a child of this item" as
    /// (item, item.rowCount()), the same convention QTreeView uses.
    VirtualItemView::DropTarget resolveDropTarget(const QPoint &viewportPos) const override;
    QRect resolveDropIndicatorRect(const DropTarget &target) const override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void connectModelSignals(QAbstractItemModel *model);
    void onStructureChanged();
    /// A column change only invalidates the cells the visible-row index points at (the row
    /// structure - and therefore the measured heights and the anchor - stay valid).
    void onColumnStructureChanged();
    void refreshVisibility(bool keepAnchor);
    void toggleIfBranchClicked(const QPoint &viewportPos, bool *handled);
    /// Invalidates the strip that can contain branch indicators (the previously
    /// invalidated strip too, so an indicator that scrolled away is erased).
    void invalidateBranchIndicators();
    /// Paints the indicators of every visible cell into \a painter.
    void paintBranchIndicators(QPainter *painter);
    /// Cell rect (viewport coordinates) of \a cellDepth of a view row.
    QRect branchCellRect(qsizetype row, int cellDepth) const;
    /// x of the branch indicator of a view row.
    int branchIndicatorX(qsizetype row) const;
    bool hasBranchIndicator(qsizetype row) const;
    /// True when \a viewportPos lies in the indicator zone of \a index.
    bool isIndicatorPosition(const QModelIndex &index, const QPoint &viewportPos) const;
    /// True when the model flags of \a index accept a drop *into* it.
    bool acceptsDropInto(const QModelIndex &index) const;
    /// Anchors an insertion line at the boundary of the visible tree, so a target
    /// inside a collapsed branch (or below the last row) still has a place.
    QRect insertionLineRect(const DropTarget &target) const;

    TreeVisibilityIndex *m_visibility = nullptr;
    ListLayout *m_rowLayout = nullptr;
    QPersistentModelIndex m_rootIndex;
    int m_indentation = 20;
    bool m_branchIndicatorsVisible = true;
    BranchIndicatorRenderer *m_branchRenderer = nullptr;
    bool m_ownBranchRenderer = false;
    /// Width of the strip invalidated for the branch indicators.
    int m_indicatorStripWidth = 0;
    /// Set when the press of the current gesture already toggled an indicator:
    /// the release must not report a click and the double click of the same
    /// gesture must not toggle a second time.
    bool m_indicatorPressToggled = false;
    bool m_updatingVisibility = false;
};

} // namespace viv
