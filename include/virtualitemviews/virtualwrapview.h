#pragma once

#include <virtualitemviews/virtualitemview.h>
#include <QMargins>

namespace viv {
class WrapItemLayout;

/// Variable-width items packed into display rows of uniform height. Model rows
/// remain item identities; SelectRows never selects a whole packed display row.
/// Width priority: explicit override, SizeHintRole, adapter estimate, 100 pixels.
/// Scrolling reuses cached geometry. Initial layout and width reflow are O(N).
/// Inherited size-index heights, row panes and row-spacing widgets are unsupported.
class VIRTUALITEMVIEWS_EXPORT VirtualWrapView : public VirtualItemView
{
    Q_OBJECT
public:
    explicit VirtualWrapView(QWidget *parent = nullptr);
    void setModel(QAbstractItemModel *model) override;
    void setAdapter(WidgetAdapter *adapter, bool takeOwnership = false) override;
    /// Displays the first-column children of this index.
    void setRootIndex(const QModelIndex &index);
    QModelIndex rootIndex() const { return m_root; }
    void setFixedItemHeight(int height);
    int fixedItemHeight() const;
    /// Natural widths are clamped to this range, then to the available line width.
    void setItemWidthRange(int minimum, int maximum);
    int minimumItemWidth() const;
    int maximumItemWidth() const;
    /// Overrides follow persistent item identity across inserts, moves and sorting.
    void setItemWidth(const QModelIndex &index, int width);
    /// Applies several explicit widths with one anchor capture and one invalidation.
    void setItemWidths(const QVector<QPair<QModelIndex, int>> &widths);
    void clearItemWidth(const QModelIndex &index);
    void clearItemWidths();
    /// Refresh estimates after an external change, for example the adapter's font.
    void invalidateItemWidths();
    void setItemSpacing(int pixels);
    int itemSpacing() const;
    void setRowSpacing(int pixels) override;
    void setContentMargins(const QMargins &margins);
    QMargins contentMargins() const;
    /// Logical leading/center/trailing; AlignAbsolute keeps physical sides in RTL.
    void setLineAlignment(Qt::Alignment alignment);
    Qt::Alignment lineAlignment() const;
    qsizetype displayRowCount() const;
    qsizetype displayRowForIndex(const QModelIndex &index) const;
    /// State shapes cover individual item bounds, never trailing blank space.
    void setVisualStateBackgroundVisible(bool visible);
    bool visualStateBackgroundVisible() const { return m_statesVisible; }
    void setStateBackgroundCornerRadius(int pixels);
    int stateBackgroundCornerRadius() const { return m_radius; }

signals:
    /// Emitted after a pass commits changed item geometry.
    void wrapLayoutChanged();

protected:
    qsizetype viewItemCount() const override;
    QModelIndex viewIndex(qsizetype item, int column = 0) const override;
    qsizetype viewItemForIndex(const QModelIndex &index) const override;
    bool isLayoutParent(const QModelIndex &parent) const override;
    bool preservesItemsOnLayoutChange() const override { return true; }
    bool isPersistentRowStateValid(const QModelIndex &index) const override;
    bool usesExplicitRowSelection() const override { return true; }
    quint64 viewMappingSerial() const override { return m_revision; }
    int estimateItemSize(qsizetype) const override;
    void applyRowSpacingOverrides() override;
    bool canMeasureItem(qsizetype) const override { return false; }
    bool supportsItemHeightConfiguration() const override { return false; }
    bool supportsRowPanes() const override { return false; }
    bool supportsRowSpacingWidgets() const override { return false; }
    qsizetype anchorItemAtOffset(qint64 offset) const override;
    qint64 autoHeightContentExtent() const override;
    void afterMaterialize() override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void refreshVisualStates() override;
    void refreshVisualState(const QModelIndex &index) override;
    bool handleItemKeyPress(QKeyEvent *event) override;
    QPixmap grabDragPreview(QWidget *widget, const QRect &capture) override;
    DropTarget resolveDropTarget(const QPoint &position) const override;
    QRect resolveDropIndicatorRect(const DropTarget &target) const override;
    Qt::Orientation dropIndicatorOrientation() const override { return Qt::Vertical; }

private:
    void reflow(const ScrollAnchor &anchor);
    void refreshWidths(int first, int last);
    void rekeyWidths();
    void settleViewportWidth();
    WrapItemLayout *m_wrap = nullptr;
    QPersistentModelIndex m_root;
    QHash<QPersistentModelIndex, int> m_widthOverrides;
    quint64 m_revision = 0;
    quint64 m_reportedRevision = 0;
    int m_reportedWidth = -1;
    bool m_hadRoot = false;
    bool m_refreshingWidths = false;
    bool m_widthRefreshPending = false;
    bool m_settlePending = false;
    bool m_statesVisible = true;
    int m_radius = 0;
};

} // namespace viv
