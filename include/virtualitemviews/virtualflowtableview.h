#pragma once

#include <virtualitemviews/virtualitemview.h>
#include <QMargins>

namespace viv {

class FlowTableLayout;

/// Responsive item grid. Display columns share the viewport width; model rows
/// remain item identities when resizing moves items between display rows.
/// Use the cell height API below. Inherited size-index height setters and
/// automatic sizeHint measurement are unsupported and report a warning.
class VIRTUALITEMVIEWS_EXPORT VirtualFlowTableView : public VirtualItemView
{
    Q_OBJECT
public:
    enum class CellHeightMode { Fixed, AspectRatio };
    Q_ENUM(CellHeightMode)

    explicit VirtualFlowTableView(QWidget *parent = nullptr);
    void setModel(QAbstractItemModel *model) override;
    /// Shows the first-column children of this model index.
    void setRootIndex(const QModelIndex &index);
    QModelIndex rootIndex() const { return m_root; }
    /// Preferred range. Filling wins when spacing/narrow sizes make it infeasible.
    void setCellWidthRange(int minimum, int maximum);
    int minimumCellWidth() const;
    int maximumCellWidth() const;
    void setCellHeightMode(CellHeightMode mode);
    CellHeightMode cellHeightMode() const;
    void setFixedCellHeight(int height);
    int fixedCellHeight() const;
    /// Ratio is width / height; nonpositive or nonfinite values are rejected.
    void setCellAspectRatio(qreal ratio);
    qreal cellAspectRatio() const;
    void setColumnSpacing(int pixels);
    int columnSpacing() const;
    void setRowSpacing(int pixels) override;
    void setContentMargins(const QMargins &margins);
    QMargins contentMargins() const;
    /// Built-in item backgrounds; business widgets should be transparent to show them.
    void setVisualStateBackgroundVisible(bool visible);
    bool visualStateBackgroundVisible() const { return m_backgroundVisible; }
    /// Rounded shape applies to the complete background, including covered gaps.
    void setStateBackgroundCornerRadius(int pixels);
    int stateBackgroundCornerRadius() const { return m_cornerRadius; }
    /// Row gaps belong to the preceding display row; coverage defaults to false.
    void setHoverBackgroundThroughRowSpacing(bool enabled);
    bool hoverBackgroundThroughRowSpacing() const { return m_hoverThroughRows; }
    void setSelectedBackgroundThroughRowSpacing(bool enabled);
    bool selectedBackgroundThroughRowSpacing() const { return m_selectedThroughRows; }
    /// Column gaps belong to the preceding item; coverage defaults to true.
    void setHoverBackgroundThroughColumnSpacing(bool enabled);
    bool hoverBackgroundThroughColumnSpacing() const { return m_hoverThroughColumns; }
    void setSelectedBackgroundThroughColumnSpacing(bool enabled);
    bool selectedBackgroundThroughColumnSpacing() const { return m_selectedThroughColumns; }
    /// Separators remain visible at zero spacing and overlay business widgets.
    void setVerticalGridLinesVisible(bool visible);
    bool verticalGridLinesVisible() const { return m_verticalLines; }
    void setHorizontalGridLinesVisible(bool visible);
    bool horizontalGridLinesVisible() const { return m_horizontalLines; }
    /// Line widths are physical pixels, clamped to at least one. Invalid colors use palette Mid.
    void setVerticalGridLineWidth(int pixels);
    int verticalGridLineWidth() const { return m_verticalLineWidth; }
    void setHorizontalGridLineWidth(int pixels);
    int horizontalGridLineWidth() const { return m_horizontalLineWidth; }
    void setVerticalGridLineColor(const QColor &color);
    QColor verticalGridLineColor() const { return m_verticalLineColor; }
    void setHorizontalGridLineColor(const QColor &color);
    QColor horizontalGridLineColor() const { return m_horizontalLineColor; }
    /// Whether separator segments bridge gaps in the other direction (default true).
    void setVerticalSpacingLineThroughRowSpacing(bool enabled);
    bool verticalSpacingLineThroughRowSpacing() const { return m_verticalThroughRows; }
    void setHorizontalSpacingLineThroughColumnSpacing(bool enabled);
    bool horizontalSpacingLineThroughColumnSpacing() const { return m_horizontalThroughColumns; }
    /// Display columns are independent of the number of items in the model.
    int columnCount() const;
    qsizetype displayRowCount() const;
    int columnWidth(int column) const;
    int columnViewportX(int column) const;
    qsizetype displayRowForIndex(const QModelIndex &index) const;
    int displayColumnForIndex(const QModelIndex &index) const;

signals:
    /// The committed viewport width, column geometry, row height or count changed.
    void flowLayoutChanged();

protected:
    qsizetype viewItemCount() const override;
    QModelIndex viewIndex(qsizetype item, int column = 0) const override;
    qsizetype viewItemForIndex(const QModelIndex &index) const override;
    bool isLayoutParent(const QModelIndex &parent) const override;
    bool preservesItemsOnLayoutChange() const override { return true; }
    bool isPersistentRowStateValid(const QModelIndex &index) const override;
    bool usesExplicitRowSelection() const override { return true; }
    quint64 viewMappingSerial() const override { return m_revision; }
    bool canMeasureItem(qsizetype) const override { return false; }
    bool supportsItemHeightConfiguration() const override { return false; }
    bool supportsRowPanes() const override { return false; }
    bool supportsRowSpacingWidgets() const override { return false; }
    void resizeEvent(QResizeEvent *event) override;
    void afterMaterialize() override;
    void paintEvent(QPaintEvent *event) override;
    void refreshVisualStates() override;
    void refreshVisualState(const QModelIndex &index) override;
    /// Uses the same exclusive pixel edges for the complete state shape and clip.
    void paintVisualStateBackgrounds(QPainter *painter, const QRect &dirty) const;
    /// Paints visible item boundaries once, without outer or absent-cell borders.
    void paintGridLines(QPainter *painter) const;
    bool handleItemKeyPress(QKeyEvent *event) override;
    DropTarget resolveDropTarget(const QPoint &position) const override;
    QRect resolveDropIndicatorRect(const DropTarget &target) const override;

private:
    void reflow(const ScrollAnchor &anchor);
    void stabilizeScrollBar();
    FlowTableLayout *m_flow = nullptr;
    QPersistentModelIndex m_root;
    quint64 m_revision = 0;
    quint64 m_modelRequest = 0;
    quint64 m_reportedRevision = 0;
    QSize m_reportedSize;
    qsizetype m_reportedCount = -1;
    int m_reportedColumns = 0;
    int m_reportedHeight = 0;
    bool m_stabilizing = false;
    ScrollAnchor m_removalAnchor;
    QVector<QMetaObject::Connection> m_modelConnections;
    QWidget *m_gridOverlay = nullptr;
    QColor m_verticalLineColor;
    QColor m_horizontalLineColor;
    int m_verticalLineWidth = 1;
    int m_horizontalLineWidth = 1;
    int m_cornerRadius = 0;
    bool m_backgroundVisible = true;
    bool m_hoverThroughRows = false;
    bool m_selectedThroughRows = false;
    bool m_hoverThroughColumns = true;
    bool m_selectedThroughColumns = true;
    bool m_verticalLines = true;
    bool m_horizontalLines = true;
    bool m_verticalThroughRows = true;
    bool m_horizontalThroughColumns = true;
};

} // namespace viv
