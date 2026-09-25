#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/tablepane.h>
#include <virtualitemviews/tablespan.h>
#include <virtualitemviews/widgetadapter.h>

#include <QHash>
#include <QRect>
#include <QSet>
#include <QWidget>

namespace viv {

/// Span decisions the framework made for one row (§43 "spans", docs/spans.md).
///
/// The framework fills this while it lays a row out, business code only reads
/// it. Without spans it stays empty and every column keeps its own rectangle, so
/// an adapter that ignores spans behaves exactly as before.
class TableSpanContext
{
public:
    bool isEmpty() const { return m_spans.isEmpty() && m_covered.isEmpty(); }
    /// Span of \a logicalColumn in this row: 1x1 unless the column anchors one.
    TableSpan spanOf(int logicalColumn) const { return m_spans.value(logicalColumn, TableSpan()); }
    /// True when another column's span covers \a logicalColumn (it has no widget
    /// of its own, and no rectangle).
    bool isCovered(int logicalColumn) const { return m_covered.contains(logicalColumn); }
    /// Merged rectangle of an anchored column, in row widget coordinates,
    /// clipped to this row. Empty for a covered or unknown column.
    QRect rect(int logicalColumn) const { return m_rects.value(logicalColumn); }

private:
    friend class VirtualTableView;

    QHash<int, TableSpan> m_spans;
    QHash<int, QRect> m_rects;
    QSet<int> m_covered;
};

/// Committed column geometry handed to the adapter while a row widget is laid
/// out (architecture document §26).
///
/// It is a light view over HeaderGeometry: column() queries the geometry on
/// demand, so no per-row copy of the column state exists.
class TableRowLayoutContext
{
public:
    TableRowLayoutContext() = default;

    /// Number of logical columns of the model.
    int columnCount() const { return m_columnCount; }
    bool isEmpty() const { return m_columnCount <= 0; }

    /// Committed geometry of a column in viewport coordinates.
    ColumnGeometry column(int logicalIndex) const;
    /// x position of a column inside the row widget.
    int columnX(int logicalIndex) const;

    /// The materialized row widget's rect in viewport coordinates.
    QRect viewportRect() const { return m_viewportRect; }
    /// Horizontal scroll offset of the body (pixels).
    qint64 horizontalOffset() const { return m_horizontalOffset; }
    /// Columns to lay out: visible range widened by the horizontal overscan.
    VisibleRange visibleColumns() const { return m_visibleColumns; }
    /// Logical indices to lay out, left to right (hidden columns skipped).
    /// Frozen columns are always included (§31).
    QVector<int> columnsToLayout() const;

    // -- panes (§31) ---------------------------------------------------------
    /// Pane a column is painted in; frozen columns never scroll, so their x
    /// does not depend on horizontalOffset().
    TablePane::Type pane(int logicalIndex) const;
    bool isColumnFrozen(int logicalIndex) const;
    /// Pane rect in the viewport; the row widget covers the whole viewport, so
    /// it is also the pane rect inside the row widget.
    QRect paneRect(TablePane::Type type) const;
    /// Frozen left/right pane widths (0 when the pane does not exist).
    int frozenLeftWidth() const;
    int frozenRightWidth() const;
    /// Container the framework puts the scrollable columns into, so they are
    /// clipped to the scrollable pane (§31). Custom layoutRowWidget() code that
    /// lays out its own children (instead of ColumnHost) must parent them here,
    /// otherwise they stay visible under a frozen pane. Null when no column is
    /// frozen.
    ///
    /// It is the container of the primary pane; with several scroll groups use
    /// paneHostForColumn() / paneHost() so every pane is clipped against its own
    /// neighbours (§43 "advanced panes").
    QWidget *scrollablePaneHost() const { return m_scrollablePaneHost; }
    /// Clip container of a pane (null for a frozen pane and when the panes do not
    /// need clipping).
    QWidget *paneHost(int paneIndex) const { return m_paneHosts.value(paneIndex, nullptr); }
    /// Clip container of the pane that shows \a logicalIndex (null for a frozen
    /// column).
    QWidget *paneHostForColumn(int logicalIndex) const
    {
        return paneHost(paneIndex(logicalIndex));
    }
    /// Index of the pane that shows \a logicalIndex (-1 when hidden/unknown).
    int paneIndex(int logicalIndex) const
    {
        return m_panes ? m_panes->paneIndexOfColumn(logicalIndex) : -1;
    }
    /// Pane at \a paneIndex; an invalid pane when it does not exist.
    TablePane paneAt(int paneIndex) const
    {
        return m_panes ? m_panes->paneAt(paneIndex) : TablePane();
    }
    /// Rect of the pane at \a paneIndex in viewport coordinates (empty when it
    /// does not exist).
    QRect paneRectAt(int paneIndex) const { return paneAt(paneIndex).viewportRect; }

    // -- spans (§43) ---------------------------------------------------------
    /// Span decisions of this row. Empty when nothing is merged.
    const TableSpanContext &spans() const { return m_spans; }

private:
    friend class VirtualTableView;

    const HeaderGeometry *m_geometry = nullptr;
    const TablePaneLayout *m_panes = nullptr;
    QWidget *m_scrollablePaneHost = nullptr;
    /// Pane index -> framework clip container of that pane (§43).
    QHash<int, QWidget *> m_paneHosts;
    TableSpanContext m_spans;
    int m_columnCount = 0;
    QRect m_viewportRect;
    qint64 m_horizontalOffset = 0;
    int m_columnOverscan = 0;
    VisibleRange m_visibleColumns;
};

inline ColumnGeometry TableRowLayoutContext::column(int logicalIndex) const
{
    if (!m_geometry || logicalIndex < 0 || logicalIndex >= m_columnCount)
        return ColumnGeometry();
    ColumnGeometry geometry = m_geometry->columnGeometry(logicalIndex);
    if (m_panes) {
        // Pane aware: a frozen column keeps its own x, the scrollable ones are
        // shifted by the horizontal offset.
        const int x = m_panes->columnViewportX(logicalIndex);
        if (x >= 0)
            geometry.viewportX = x;
    }
    return geometry;
}

inline int TableRowLayoutContext::columnX(int logicalIndex) const
{
    const ColumnGeometry geometry = column(logicalIndex);
    return geometry.isValid() ? geometry.viewportX - m_viewportRect.x() : 0;
}

inline QVector<int> TableRowLayoutContext::columnsToLayout() const
{
    if (m_panes)
        return m_panes->columnsForLayout(m_columnOverscan);
    QVector<int> columns;
    if (!m_geometry || !m_visibleColumns.isValid())
        return columns;
    columns.reserve(int(m_visibleColumns.count()));
    for (qsizetype visual = m_visibleColumns.first; visual <= m_visibleColumns.last; ++visual) {
        const int logical = m_geometry->logicalIndex(int(visual));
        if (logical < 0 || m_geometry->isSectionHidden(logical))
            continue;
        columns.append(logical);
    }
    return columns;
}

inline TablePane::Type TableRowLayoutContext::pane(int logicalIndex) const
{
    return m_panes ? m_panes->paneOfColumn(logicalIndex) : TablePane::Type::Scrollable;
}

inline bool TableRowLayoutContext::isColumnFrozen(int logicalIndex) const
{
    return pane(logicalIndex) != TablePane::Type::Scrollable;
}

inline QRect TableRowLayoutContext::paneRect(TablePane::Type type) const
{
    return m_panes ? m_panes->paneRect(type) : QRect();
}

inline int TableRowLayoutContext::frozenLeftWidth() const
{
    return m_panes ? m_panes->frozenLeftWidth() : 0;
}

inline int TableRowLayoutContext::frozenRightWidth() const
{
    return m_panes ? m_panes->frozenRightWidth() : 0;
}

/// Convenience container for a business cell widget (architecture document §27).
///
/// The framework keeps the host aligned with its column (position and
/// visibility); business code only fills the host's content.
class VIRTUALITEMVIEWS_EXPORT ColumnHost : public QWidget
{
    Q_OBJECT

public:
    explicit ColumnHost(int logicalColumn, QWidget *parent = nullptr);

    int logicalColumn() const { return m_logicalColumn; }
    void setLogicalColumn(int logicalColumn);

private:
    int m_logicalColumn = -1;
};

/// Adapter contract of the table body: WidgetAdapter plus an optional hook to
/// lay the row widget out according to the committed column geometry.
///
/// Adapters that use ColumnHost children do not have to implement the hook at
/// all: the framework positions the hosts itself.
class VIRTUALITEMVIEWS_EXPORT TableWidgetAdapter : public WidgetAdapter
{
public:
    /// Called after bindWidget() and whenever the column geometry or the
    /// horizontal offset changes.
    virtual void layoutRowWidget(QWidget *widget, const QModelIndex &rowIndex,
                                 const TableRowLayoutContext &context)
    {
        Q_UNUSED(widget);
        Q_UNUSED(rowIndex);
        Q_UNUSED(context);
    }
};

/// Adapter of the cell widgets for Cell Widget Mode (architecture document §28).
///
/// Only `visibleRows x visibleColumns` cells own a QWidget; a pooled cell widget
/// is unbound and must not keep the state of its previous index.
class VIRTUALITEMVIEWS_EXPORT CellWidgetAdapter
{
public:
    virtual ~CellWidgetAdapter() = default;

    /// Classifies an index into a recycler pool. The default keeps one pool.
    virtual WidgetType cellWidgetType(const QModelIndex &index) const
    {
        Q_UNUSED(index);
        return kDefaultWidgetType;
    }

    virtual QWidget *createCellWidget(WidgetType type, QWidget *parent) = 0;

    /// Fills \a widget with the data of \a index; called before the widget is
    /// shown and every time the cell is reused.
    virtual void bindCellWidget(QWidget *widget, const QModelIndex &index) = 0;

    /// Detaches \a widget from \a index (stop timers, animations, subscriptions).
    virtual void unbindCellWidget(QWidget *widget, const QModelIndex &index)
    {
        Q_UNUSED(widget);
        Q_UNUSED(index);
    }
};

} // namespace viv
