#pragma once

#include <virtualitemviews/virtualitemview.h>

#include <QMargins>
#include <QMetaObject>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>

class QPainter;
class QWidget;
class QVariantAnimation;

namespace viv {

class MasonryLayout;

/// Virtualized shortest-column layout for items with independent heights.
class VIRTUALITEMVIEWS_EXPORT VirtualMasonryView : public VirtualItemView
{
    Q_OBJECT
public:
    enum class ItemHeightMode { Fixed, AspectRatio, AdapterEstimate };
    Q_ENUM(ItemHeightMode)

    explicit VirtualMasonryView(QWidget *parent = nullptr);
    void setModel(QAbstractItemModel *model) override;
    /// Installs the adapter and refreshes estimates for the current model.
    void setAdapter(WidgetAdapter *adapter, bool takeOwnership = false) override;
    /// Shows the first-column children of this model index. Removing the root
    /// or one of its ancestors returns the view to top-level items.
    void setRootIndex(const QModelIndex &index);
    QModelIndex rootIndex() const { return m_root; }

    void setCellWidthRange(int minimum, int maximum);
    int minimumCellWidth() const;
    int maximumCellWidth() const;
    /// Zero selects automatic column count; positive values request a fixed count.
    void setColumnCount(int count);
    int columnCount() const;
    void setColumnSpacing(int pixels);
    int columnSpacing() const;
    void setRowSpacing(int pixels) override;
    int rowSpacing() const;
    void setContentMargins(const QMargins &margins);
    QMargins contentMargins() const;

    void setItemHeightMode(ItemHeightMode mode);
    ItemHeightMode itemHeightMode() const;
    void setFixedItemHeight(int height);
    int fixedItemHeight() const;
    void setItemAspectRatio(qreal widthOverHeight);
    qreal itemAspectRatio() const;
    /// Supplies a size estimate without creating the item's QWidget.
    void setItemEstimatedSize(const QModelIndex &index, const QSize &size);
    /// Replaces all estimates in one rebuild, useful for image manifests.
    void setItemEstimatedSizes(const QVector<QSize> &sizes);
    /// Supplies an actual/authoritative height and rebuilds affected geometry.
    void setItemHeight(const QModelIndex &index, int height);
    /// Queues an adapter estimate refresh; notifications in one turn share a rebuild.
    void invalidateItemSize(const QModelIndex &index);
    /// Animates authoritative height changes when enabled; disabled by default.
    void setItemHeightAnimationEnabled(bool enabled);
    bool itemHeightAnimationEnabled() const { return m_heightAnimationEnabled; }
    void setItemHeightAnimationDuration(int milliseconds);
    int itemHeightAnimationDuration() const { return m_heightAnimationDuration; }

    int itemColumn(const QModelIndex &index) const;
    QRect visualRect(const QModelIndex &index) const;
    QModelIndex indexAt(const QPoint &position) const;

    void setVisualStateBackgroundVisible(bool visible);
    bool visualStateBackgroundVisible() const { return m_backgroundVisible; }
    void setStateBackgroundCornerRadius(int pixels);
    int stateBackgroundCornerRadius() const { return m_cornerRadius; }

    /// Separators are painted above materialized item widgets.
    void setVerticalGridLinesVisible(bool visible);
    bool verticalGridLinesVisible() const { return m_verticalLines; }
    void setHorizontalGridLinesVisible(bool visible);
    bool horizontalGridLinesVisible() const { return m_horizontalLines; }
    void setVerticalGridLineWidth(int pixels);
    int verticalGridLineWidth() const { return m_verticalLineWidth; }
    void setHorizontalGridLineWidth(int pixels);
    int horizontalGridLineWidth() const { return m_horizontalLineWidth; }
    void setVerticalGridLineColor(const QColor &color);
    QColor verticalGridLineColor() const { return m_verticalLineColor; }
    void setHorizontalGridLineColor(const QColor &color);
    QColor horizontalGridLineColor() const { return m_horizontalLineColor; }
    /// Whether vertical separators bridge gaps between stacked items.
    void setVerticalSpacingLineThroughRowSpacing(bool enabled);
    bool verticalSpacingLineThroughRowSpacing() const { return m_verticalThroughRows; }
    /// Whether horizontal separators bridge the gap into the next column.
    void setHorizontalSpacingLineThroughColumnSpacing(bool enabled);
    bool horizontalSpacingLineThroughColumnSpacing() const { return m_horizontalThroughColumns; }

signals:
    void masonryLayoutChanged();

protected:
    qsizetype viewItemCount() const override;
    QModelIndex viewIndex(qsizetype item, int column = 0) const override;
    qsizetype viewItemForIndex(const QModelIndex &index) const override;
    bool isLayoutParent(const QModelIndex &parent) const override;
    bool preservesItemsOnLayoutChange() const override { return true; }
    quint64 viewMappingSerial() const override { return m_revision; }
    bool isPersistentRowStateValid(const QModelIndex &index) const override;
    bool canMeasureItem(qsizetype) const override { return false; }
    bool supportsItemHeightConfiguration() const override { return false; }
    bool supportsRowPanes() const override { return false; }
    bool supportsRowSpacingWidgets() const override { return false; }
    qsizetype anchorItemAtOffset(qint64 offset) const override;
    void resizeEvent(QResizeEvent *event) override;
    void afterMaterialize() override;
    QVector<qsizetype> materializationItems(const QVector<VisibleRange> &ranges) const override;
    bool handleItemKeyPress(QKeyEvent *event) override;
    DropTarget resolveDropTarget(const QPoint &position) const override;
    QRect resolveDropIndicatorRect(const DropTarget &target) const override;
    void paintEvent(QPaintEvent *event) override;
    void refreshVisualStates() override;
    void refreshVisualState(const QModelIndex &index) override;
    /// Uses the same exclusive pixel edges for the complete state shape and clip.
    void paintVisualStateBackgrounds(QPainter *painter, const QRect &dirty) const;
    /// Paints visible item boundaries once, without outer or absent-item borders.
    void paintGridLines(QPainter *painter) const;

private:
    struct SavedItemSize {
        QPersistentModelIndex index;
        QSize estimate;
        int height = 0;
    };
    struct ColumnChange {
        QPersistentModelIndex root;
        qsizetype anchorItem = -1;
        int offsetInsideItem = 0;
        bool pending = false;
    };

    void connectEstimateSignals(QAbstractItemModel *model);
    void saveItemSizes();
    void restoreItemSizes();
    void refreshAdapterEstimates(const ScrollAnchor &preservedAnchor = ScrollAnchor());
    void applyPendingItemSizes();
    void refreshAdapterEstimateRange(const QModelIndex &topLeft,
                                     const QModelIndex &bottomRight);
    void reflow(const ScrollAnchor &anchor);
    void finishItemHeightAnimation();
    void applyItemHeight(qsizetype item, int height);
    void animateItemHeight(qsizetype item, int height);
    MasonryLayout *m_masonry = nullptr;
    QPersistentModelIndex m_root;
    bool m_rootRemoving = false;
    quint64 m_modelRequest = 0;
    QVector<QMetaObject::Connection> m_modelConnections;
    QVector<QMetaObject::Connection> m_estimateConnections;
    QVector<QPersistentModelIndex> m_pendingItemSizes;
    QVector<SavedItemSize> m_savedItemSizes;
    ScrollAnchor m_sizeChangeAnchor;
    ColumnChange m_columnChange;
    bool m_itemSizesScheduled = false;
    QVariantAnimation *m_heightAnimation = nullptr;
    qsizetype m_animatingItem = -1;
    int m_animationTargetHeight = 0;
    bool m_heightAnimationEnabled = false;
    int m_heightAnimationDuration = 300;
    quint64 m_revision = 0;
    QWidget *m_gridOverlay = nullptr;
    QColor m_verticalLineColor;
    QColor m_horizontalLineColor;
    int m_verticalLineWidth = 1;
    int m_horizontalLineWidth = 1;
    int m_cornerRadius = 0;
    bool m_backgroundVisible = true;
    bool m_verticalLines = true;
    bool m_horizontalLines = true;
    bool m_verticalThroughRows = true;
    bool m_horizontalThroughColumns = true;

};

} // namespace viv
