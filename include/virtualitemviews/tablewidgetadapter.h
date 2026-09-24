#pragma once

#include <virtualitemviews/headergeometry.h>
#include <virtualitemviews/widgetadapter.h>

#include <QRect>
#include <QWidget>

namespace viv {

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
    QVector<int> columnsToLayout() const;

private:
    friend class VirtualTableView;

    const HeaderGeometry *m_geometry = nullptr;
    int m_columnCount = 0;
    QRect m_viewportRect;
    qint64 m_horizontalOffset = 0;
    VisibleRange m_visibleColumns;
};

inline ColumnGeometry TableRowLayoutContext::column(int logicalIndex) const
{
    if (!m_geometry || logicalIndex < 0 || logicalIndex >= m_columnCount)
        return ColumnGeometry();
    return m_geometry->columnGeometry(logicalIndex);
}

inline int TableRowLayoutContext::columnX(int logicalIndex) const
{
    const ColumnGeometry geometry = column(logicalIndex);
    return geometry.isValid() ? geometry.viewportX - m_viewportRect.x() : 0;
}

inline QVector<int> TableRowLayoutContext::columnsToLayout() const
{
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

/// Convenience container for a business cell widget (architecture document §27).
///
/// The framework keeps the host aligned with its column (position and
/// visibility); business code only fills the host's content.
class ColumnHost : public QWidget
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
class TableWidgetAdapter : public WidgetAdapter
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
class CellWidgetAdapter
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
