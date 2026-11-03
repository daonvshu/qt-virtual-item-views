#pragma once

#include <virtualitemviews/branchindicator.h>
#include <virtualitemviews/treevisibilityindex.h>
#include <virtualitemviews/virtualtableview.h>

#include <QColor>
#include <QHash>
#include <QPersistentModelIndex>
#include <QPointer>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

namespace viv {

class TreeExpansionTransition;
class TreeDragPlaceholderOutline;

/// A multi-column table whose rows are the expanded, visible nodes of a model tree.
/// Column zero owns the branch indicator even when columns are reordered.
/// Column freezing is supported; row freezing is not.
class VIRTUALITEMVIEWS_EXPORT VirtualTreeTableView : public VirtualTableView
{
    Q_OBJECT

public:
    explicit VirtualTreeTableView(QWidget *parent = nullptr);
    ~VirtualTreeTableView() override;

    void setModel(QAbstractItemModel *model) override;
    /// Restrict the view to the children of this node; column zero identifies it.
    void setRootIndex(const QModelIndex &index);
    QModelIndex rootIndex() const { return m_rootIndex; }

    /// Expansion changes the visible row sequence, not the model hierarchy.
    void expand(const QModelIndex &index);
    void collapse(const QModelIndex &index);
    void expandRecursively(const QModelIndex &index);
    void collapseAll();
    /// Live layout transition; enabled by default. Collapse retains outgoing
    /// rows until completion, then emits collapsed(). Bulk collapseAll()
    /// updates are immediate, without a screenshot fallback.
    void setExpansionAnimationEnabled(bool enabled);
    bool expansionAnimationEnabled() const;
    /// Duration in milliseconds; zero disables the visual transition.
    void setExpansionAnimationDuration(int milliseconds);
    int expansionAnimationDuration() const;
    /// Fade transitioning descendants with the live layout animation; default true.
    /// Applies to row/cell widgets and vertical headers, preserving item graphics effects.
    void setExpansionFadeEnabled(bool enabled);
    bool expansionFadeEnabled() const;
    void setExpanded(const QModelIndex &index, bool expanded);
    void toggleExpanded(const QModelIndex &index);
    bool isExpanded(const QModelIndex &index) const;
    bool hasChildren(const QModelIndex &index) const;
    qsizetype visibleRowCount() const;
    TreeVisibilityIndex *visibilityIndex() const { return m_visibility; }
    int itemDepth(const QModelIndex &index) const override;

    /// The branch decoration occupies one indentation cell per ancestor and node.
    void setIndentation(int pixels);
    int indentation() const { return m_indentation; }
    /// Default true. Hiding removes branch decoration and the node's own icon
    /// cell, while preserving depth * indentation() inside column zero.
    void setBranchAreaVisible(bool visible);
    bool branchAreaVisible() const { return m_branchAreaVisible; }
    /// Model data on column zero: pixels below this node; missing/negative values
    /// use rowSpacing(). The last visible node has no trailing gap.
    static constexpr int NodeRowSpacingRole = Qt::UserRole + 0x100;
    /// Explicit name for the existing below-node role.
    static constexpr int NodeRowSpacingBelowRole = NodeRowSpacingRole;
    /// Pixels above this node, added to the preceding node's below gap.
    /// Missing/negative values mean zero; the first visible row has no leading gap.
    static constexpr int NodeRowSpacingAboveRole = Qt::UserRole + 0x101;
    /// Row lines span the actual columns, including the branch and indentation areas.
    /// Row line style is the table's horizontal grid line style.
    void setRowGridLinesVisible(bool visible) { setHorizontalGridLinesVisible(visible); }
    bool rowGridLinesVisible() const { return horizontalGridLinesVisible(); }
    void setRowGridLineWidth(int pixels) { setHorizontalGridLineWidth(pixels); }
    int rowGridLineWidth() const { return horizontalGridLineWidth(); }
    void setRowGridLineColor(const QColor &color) { setHorizontalGridLineColor(color); }
    QColor rowGridLineColor() const { return horizontalGridLineColor(); }
    /// Optional viewport-painted hover and selection backgrounds, including tree decoration.
    /// Uses the table's row/cell selection behavior without a separate tree extent mode.
    void setVisualStateBackgroundVisible(bool visible) override;
    bool visualStateBackgroundVisible() const { return m_visualStateBackgroundVisible; }
    void setBranchIndicatorsVisible(bool visible);
    bool branchIndicatorsVisible() const { return m_branchIndicatorsVisible; }
    /// Default renderer only: one non-null icon rotates 90 degrees with expansion.
    /// Two non-null icons switch immediately; two null icons restore the rotating
    /// built-in expanded triangle. Custom renderers keep their own state handling.
    void setExpandedBranchIcon(const QIcon &icon);
    QIcon expandedBranchIcon() const { return m_expandedBranchIcon; }
    void setCollapsedBranchIcon(const QIcon &icon);
    QIcon collapsedBranchIcon() const { return m_collapsedBranchIcon; }
    /// Square logical-pixel size (default 10), clamped to at least one and to the cell when painted.
    void setBranchIconSize(int pixels);
    int branchIconSize() const { return m_branchIconSize; }
    /// Tints icons and triangles; invalid color restores icon colors / palette WindowText.
    void setBranchIconColor(const QColor &color);
    QColor branchIconColor() const { return m_branchIconColor; }
    /// Alignment of the default icon or built-in triangle inside its branch cell.
    /// Missing horizontal/vertical flags fall back to AlignLeft/AlignVCenter.
    /// Defaults to Qt::AlignCenter; custom renderers control their own layout.
    void setBranchIndicatorAlignment(Qt::Alignment alignment);
    Qt::Alignment branchIndicatorAlignment() const { return m_branchIndicatorAlignment; }
    /// Padding around the default icon or built-in triangle, in logical pixels.
    /// Negative values are clamped to zero; the icon is clamped to the padded area.
    /// The default is centered with zero padding.
    void setBranchIndicatorPadding(const QMargins &padding);
    void setBranchIndicatorPadding(int left, int top, int right, int bottom)
    {
        setBranchIndicatorPadding(QMargins(left, top, right, bottom));
    }
    QMargins branchIndicatorPadding() const { return m_branchIndicatorPadding; }
    /// Custom renderer; null uses the configured icons or built-in triangle.
    /// Icon settings do not affect custom renderers. The view owns the renderer
    /// only when takeOwnership is true.
    void setBranchIndicatorRenderer(BranchIndicatorRenderer *renderer, bool takeOwnership = false);
    BranchIndicatorRenderer *branchIndicatorRenderer() const { return m_branchRenderer; }
    BranchIndicatorState branchState(const QModelIndex &index, int cellDepth = -1) const;

    /// The root's child-column count defines the shared header schema.
    int columnCount() const override;

signals:
    void expanded(const QModelIndex &index);
    void collapsed(const QModelIndex &index);
    void visibleRowsChanged();

protected:
    QModelIndex columnSchemaParent() const override;
    qsizetype viewItemCount() const override;
    QModelIndex viewIndex(qsizetype item, int column = 0) const override;
    qsizetype viewItemForIndex(const QModelIndex &index) const override;
    bool isLayoutParent(const QModelIndex &parent) const override;
    bool managesVisibleRows() const override { return true; }
    bool isPersistentRowStateValid(const QModelIndex &index) const override;
    QModelIndex indexForNavigation(qsizetype item, const QModelIndex &current) const override;
    QItemSelection selectionRange(const QModelIndex &anchor,
                                  const QModelIndex &target) const override;
    bool usesExplicitRowSelection() const override { return true; }
    /// Tree rows scroll together; horizontal column panes remain supported.
    bool supportsRowPanes() const override { return false; }
    void applyRowSpacingOverrides() override;
    void afterMaterialize() override;
    void visualColumnGeometryChanged() override;
    void moveRowsForStripDrag(int fromRow, int toRow) override;
    bool isSpanValid(const QModelIndex &anchor, const TableSpan &span) const override;
    QRect spanRowRect(qsizetype row) const override;
    quint64 viewMappingSerial() const override { return m_mappingSerial; }
    int leadingCellInset(const QModelIndex &index) const override;
    bool handleItemKeyPress(QKeyEvent *event) override;
    QModelIndex dragNodeIndex(const QModelIndex &index) const override;
    /// Collapse a single source immediately before capture in row animation mode.
    bool prepareDrag(const QModelIndexList &indexes) override;
    bool resolveDragRowMove(const DropTarget &target,
                           const QList<QPersistentModelIndex> &sources,
                           qsizetype *sourceRow, qsizetype *destination) override;
    void clearDragFeedback() override;
    void dragReorderFrameChanged() override;
    DropTarget resolveDropTarget(const QPoint &viewportPos) const override;
    QRect resolveDropIndicatorRect(const DropTarget &target) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void refreshVisualStates() override;
    void refreshVisualState(const QModelIndex &index) override;
    void paintEvent(QPaintEvent *event) override;

private:
    // Keep column freezing public, but exclude row freezing from the tree API.
    using VirtualItemView::setFrozenRows;
    using VirtualItemView::setFrozenBottomRows;

    /// Resolves the gap from node data, falling back to rowSpacing().
    int effectiveRowSpacing(qsizetype row) const;
    void connectTreeSignals(QAbstractItemModel *model);
    bool isNodeWithinRoot(const QModelIndex &index) const;
    void clearStateOutsideRoot();
    void clearPinsForColumnIdentityChange(const QModelIndex &parent);
    void captureColumnIdentitySelection();
    void reconcileColumnIdentitySelection();
    void onStructureChanged(bool resetSizes, bool columnChange = false);
    void refreshVisibility(bool resetSizes);
    void refreshVisibilitySplice(qsizetype first, qsizetype removed, qsizetype inserted,
                                 qsizetype previousCount);
    QModelIndex nodeIndex(const QModelIndex &index) const;
    QRect branchCellRect(qsizetype row, int cellDepth, bool visual = false) const;
    bool isIndicatorPosition(const QModelIndex &index, const QPoint &position) const;
    void paintBranches(QPainter *painter) const;
    void paintDefaultBranchIndicator(QPainter *painter, const BranchIndicatorState &state,
                                     const QModelIndex &index, const QRect &cellRect) const;
    void invalidateBranches();
    QRect insertionLineRect(const DropTarget &target) const;
    /// First visible row after a node's complete expanded subtree.
    qsizetype visibleSubtreeEnd(QModelIndex node) const;
    /// Viewport x for a destination depth inside logical column zero.
    int dropTargetLeft(const DropTarget &target, bool includeBranchCell) const;

    TreeVisibilityIndex *m_visibility = nullptr;
    TreeExpansionTransition *m_expansionTransition = nullptr;
    QPointer<TreeDragPlaceholderOutline> m_dragPlaceholderOutline;
    QPersistentModelIndex m_dragTargetParent;
    int m_dragTargetRow = -1;
    bool m_dragTargetAmbiguous = false;
    quint64 m_dragFeedbackRevision = 0;
    QPersistentModelIndex m_rootIndex;
    quint64 m_rootChangeSerial = 0;
    quint64 m_mappingSerial = 0;
    QVector<QPair<int, int>> m_headerSpacingOverrides;
    QIcon m_expandedBranchIcon;
    QIcon m_collapsedBranchIcon;
    int m_branchIconSize = kBranchIndicatorSize;
    QColor m_branchIconColor;
    Qt::Alignment m_branchIndicatorAlignment = Qt::AlignCenter;
    QMargins m_branchIndicatorPadding;
    BranchIndicatorRenderer *m_branchRenderer = nullptr;
    QWidget *m_branchOverlay = nullptr;
    int m_indentation = 20;
    bool m_ownBranchRenderer = false;
    bool m_branchIndicatorsVisible = true;
    bool m_branchAreaVisible = true;
    bool m_indicatorPressToggled = false;
    bool m_updatingVisibility = false;
    bool m_visibilityRefreshPending = false;
    bool m_visibilityRefreshQueued = false;
    bool m_rowHeaderSpacingDirty = true;
    struct SelectedCellIdentity
    {
        QPersistentModelIndex cell;
        QPersistentModelIndex node;
    };
    QVector<SelectedCellIdentity> m_columnSelectionSnapshot;
    QPersistentModelIndex m_columnCurrentNode;
    QPointer<QItemSelectionModel> m_columnSelectionModel;
    bool m_columnIdentityChangePending = false;
    bool m_columnHadCurrent = false;
    bool m_columnMappingPending = false;
};

} // namespace viv
