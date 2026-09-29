#pragma once

#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtualtableview.h>

#include <QPainterPath>
#include <QPersistentModelIndex>
#include <QRectF>

class QPainter;

namespace viv {

class StyledTableView;

/// ColumnHost that paints the view's animated cell background below its children.
class VIRTUALITEMVIEWS_EXPORT StyledTableCellHost : public ColumnHost
{
public:
    StyledTableCellHost(int logicalColumn, StyledTableView *view, QWidget *parent = nullptr);

    void bindIndex(const QModelIndex &index);
    void clearIndex();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    StyledTableView *m_view = nullptr;
    QPersistentModelIndex m_index;
};

/// Row background for TableWidgetAdapter's row-widget mode.
class VIRTUALITEMVIEWS_EXPORT StyledTableRowWidget : public QWidget
{
public:
    explicit StyledTableRowWidget(StyledTableView *view, QWidget *parent = nullptr);

    /// Bind or clear the row and every StyledTableCellHost below it.
    void bindRow(const QModelIndex &index);
    void unbindRow();
    void refreshVisualState(const QModelIndex &index);
    void refreshVisualStates();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    StyledTableView *m_view = nullptr;
    QPersistentModelIndex m_index;
};

/// Optional styled table base. Adapters provide content; row/cell hosts paint state.
class VIRTUALITEMVIEWS_EXPORT StyledTableView : public VirtualTableView
{
public:
    explicit StyledTableView(QWidget *parent = nullptr);

    void setVisualStateCornerRadius(int radius);
    int visualStateCornerRadius() const { return m_cornerRadius; }
    void setBackgroundCoversGridLines(bool enabled);
    bool backgroundCoversGridLines() const { return m_backgroundCoversGridLines; }
    void refreshRoundedEdges();
    bool hasLeftRowEdge() const;
    bool hasRightRowEdge() const;
    /// Override both hooks to customize the background shape and its painting.
    virtual QPainterPath visualStateBackgroundPath(const QRectF &rect) const;
    virtual void paintVisualStateBackground(QPainter *painter, const QRectF &rect,
                                             const QModelIndex &index) const;

protected:
    void refreshVisualStates() override;
    void refreshVisualState(const QModelIndex &index) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    class GridCoverOverlay;
    void refreshGridCover(const QRect &dirty = QRect());

    GridCoverOverlay *m_gridCover = nullptr;
    int m_cornerRadius = 6;
    bool m_backgroundCoversGridLines = false;
    bool m_leftRounded = true;
    bool m_rightRounded = true;
};

} // namespace viv
