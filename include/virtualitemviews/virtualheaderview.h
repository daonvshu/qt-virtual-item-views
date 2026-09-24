#pragma once

#include <virtualitemviews/headerwidgetadapter.h>
#include <virtualitemviews/nativeheaderview.h>

#include <QHash>
#include <QList>
#include <QPoint>
#include <QVector>
#include <QWidget>

class QAbstractItemModel;

namespace viv {

class HeaderGeometry;
class WidgetRecycler;

/// QWidget based header (architecture document §17-§19).
///
/// Instead of letting QStyle paint sections, every *materialized* section is a
/// real QWidget produced by a HeaderWidgetAdapter, so a header can carry badges,
/// progress, filter buttons, search fields or animated sort arrows. It
/// deliberately does not inherit QHeaderView: the paint/cache machinery of
/// QHeaderView and a child widget tree do not mix well.
///
/// Invariants (§19):
///  - only visible sections (+ overscan) and pinned sections own a widget, so the
///    widget count never grows with the number of columns,
///  - a pooled widget is unbound and keeps no identity,
///  - section geometry always comes from HeaderGeometry, which stays the single
///    source of truth for sizes, order, visibility and the horizontal offset,
///  - a section whose child has focus or an open popup is pinned, not recycled
///    (§36).
class VirtualHeaderView : public QWidget, public HeaderViewInterface
{
    Q_OBJECT

public:
    explicit VirtualHeaderView(Qt::Orientation orientation = Qt::Horizontal,
                               QWidget *parent = nullptr);
    ~VirtualHeaderView() override;

    // -- HeaderViewInterface -------------------------------------------------
    void setGeometryModel(HeaderGeometry *geometry) override;
    HeaderGeometry *geometryModel() const override { return m_geometry; }
    QWidget *headerWidget() override { return this; }
    Qt::Orientation orientation() const override { return m_orientation; }
    void setLabelModel(QAbstractItemModel *model) override;
    QAbstractItemModel *labelModel() const { return m_labelModel; }
    void setSortInteractionEnabled(bool enabled) override;
    bool isSortInteractionEnabled() const { return m_sortInteractionEnabled; }
    /// Restricts the header to one pane (§31): only \a logicalColumns are
    /// materialized and positioned, so the same class renders the scrollable
    /// pane and the frozen panes. \a frozen keeps the pane rect's own origin.
    void setPaneFilter(const QVector<int> &logicalColumns, bool frozen) override;
    void clearPaneFilter() override;
    bool hasPaneFilter() const { return m_paneFilterActive; }
    /// Origin of the viewport inside the view; the table sets it so section x
    /// positions can be derived from HeaderGeometry (viewport coordinates).
    void setViewportOrigin(const QPoint &origin) override;

    // -- adapter / diagnostics ----------------------------------------------
    void setAdapter(HeaderWidgetAdapter *adapter, bool takeOwnership = false);
    HeaderWidgetAdapter *adapter() const { return m_adapter; }
    WidgetRecycler *recycler() const { return m_recycler; }

    /// Sections that currently own a QWidget.
    QList<int> materializedSections() const;
    QWidget *sectionWidget(int logicalIndex) const;
    qsizetype materializedSectionCount() const { return m_sectionWidgets.size(); }
    qsizetype pooledSectionCount() const;

    /// Views in front of/behind the viewport that are kept materialized.
    void setSectionOverscan(int sections);
    int sectionOverscan() const { return m_overscan; }

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void connectGeometry(HeaderGeometry *geometry, bool connectSignals);
    void relayout();
    void recycleAllSections();
    /// x of \a logicalIndex inside this widget (-1 when hidden/unknown).
    int sectionX(int logicalIndex) const;
    int sectionAt(const QPoint &pos) const;
    /// Logical section whose leading/trailing edge is under \a pos (or -1).
    int resizeEdgeAt(const QPoint &pos) const;
    bool isSectionPinned(int logicalIndex) const;
    bool isFiltered(int logicalIndex) const;

    Qt::Orientation m_orientation = Qt::Horizontal;
    HeaderGeometry *m_geometry = nullptr;
    QAbstractItemModel *m_labelModel = nullptr;
    HeaderWidgetAdapter *m_adapter = nullptr;
    bool m_ownAdapter = false;
    WidgetRecycler *m_recycler = nullptr;

    QHash<int, QWidget *> m_sectionWidgets;
    QPoint m_viewportOrigin;
    QVector<int> m_paneFilter;
    bool m_paneFilterActive = false;
    int m_overscan = 1;
    bool m_sortInteractionEnabled = false;

    int m_resizeSection = -1;
    int m_resizeStartSize = 0;
    int m_resizeStartX = 0;
    int m_pressedSection = -1;
    int m_pressedX = 0;
    bool m_moved = false;
};

} // namespace viv
