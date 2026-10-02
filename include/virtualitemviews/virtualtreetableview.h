#pragma once

#include <virtualitemviews/branchindicator.h>
#include <virtualitemviews/treevisibilityindex.h>
#include <virtualitemviews/virtualtableview.h>

#include <QHash>
#include <QPersistentModelIndex>
#include <QPointer>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

namespace viv {

/// A multi-column table whose rows are the expanded, visible nodes of a model tree.
/// Column zero owns the branch indicator even when columns are reordered.
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
    /// Negative pixels remove the depth override and restore rowSpacing().
    void setDepthRowSpacing(int depth, int pixels);
    int depthRowSpacing(int depth) const;
    /// Row line style is the table's horizontal grid line style.
    void setRowGridLinesVisible(bool visible) { setHorizontalGridLinesVisible(visible); }
    bool rowGridLinesVisible() const { return horizontalGridLinesVisible(); }
    void setRowGridLineWidth(int pixels) { setHorizontalGridLineWidth(pixels); }
    int rowGridLineWidth() const { return horizontalGridLineWidth(); }
    void setRowGridLineColor(const QColor &color) { setHorizontalGridLineColor(color); }
    QColor rowGridLineColor() const { return horizontalGridLineColor(); }
    enum class RowGridLineExtent { NodeOnly, NodeAndIcon, FullWidth };
    Q_ENUM(RowGridLineExtent)
    /// Excludes only the tree decoration inside logical column zero.
    void setRowGridLineExtent(RowGridLineExtent extent);
    RowGridLineExtent rowGridLineExtent() const { return m_rowGridLineExtent; }
    /// Optional viewport-painted hover and selection backgrounds.
    void setVisualStateBackgroundVisible(bool visible);
    bool visualStateBackgroundVisible() const { return m_visualStateBackgroundVisible; }
    enum class VisualStateBackgroundExtent { NodeOnly, NodeAndIcon, FullWidth };
    Q_ENUM(VisualStateBackgroundExtent)
    /// Controls the painted background inside logical column zero.
    void setVisualStateBackgroundExtent(VisualStateBackgroundExtent extent);
    VisualStateBackgroundExtent visualStateBackgroundExtent() const
    {
        return m_visualStateBackgroundExtent;
    }
    void setBranchIndicatorsVisible(bool visible);
    bool branchIndicatorsVisible() const { return m_branchIndicatorsVisible; }
    /// The view owns the renderer only when takeOwnership is true.
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
    void applyRowSpacingOverrides() override;
    void configureRowSpacingWidget(QWidget *widget) const override;
    void afterMaterialize() override;
    void visualColumnGeometryChanged() override;
    void moveRowsForStripDrag(int fromRow, int toRow) override;
    bool isSpanValid(const QModelIndex &anchor, const TableSpan &span) const override;
    QRect spanRowRect(qsizetype row) const override;
    quint64 viewMappingSerial() const override { return m_mappingSerial; }
    int leadingCellInset(const QModelIndex &index) const override;
    QRect rowGridLineExclusion(int depth) const override;
    bool handleItemKeyPress(QKeyEvent *event) override;
    QModelIndex dragNodeIndex(const QModelIndex &index) const override;
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
    QRect decorationExclusion(int cells) const;
    bool isIndicatorPosition(const QModelIndex &index, const QPoint &position) const;
    void paintBranches(QPainter *painter) const;
    void paintVisualStateBackgrounds(QPainter *painter, const QRect &dirty) const;
    void invalidateBranches();
    QRect insertionLineRect(const DropTarget &target) const;

    TreeVisibilityIndex *m_visibility = nullptr;
    QPersistentModelIndex m_rootIndex;
    quint64 m_rootChangeSerial = 0;
    quint64 m_mappingSerial = 0;
    QHash<int, int> m_depthRowSpacing;
    QVector<QPair<int, int>> m_headerSpacingOverrides;
    BranchIndicatorRenderer *m_branchRenderer = nullptr;
    QWidget *m_branchOverlay = nullptr;
    int m_indentation = 20;
    RowGridLineExtent m_rowGridLineExtent = RowGridLineExtent::FullWidth;
    VisualStateBackgroundExtent m_visualStateBackgroundExtent = VisualStateBackgroundExtent::FullWidth;
    bool m_ownBranchRenderer = false;
    bool m_branchIndicatorsVisible = true;
    bool m_visualStateBackgroundVisible = false;
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
