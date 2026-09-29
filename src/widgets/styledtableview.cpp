#include <virtualitemviews/styledtableview.h>

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRegion>
#include <QResizeEvent>

namespace viv {

StyledTableCellHost::StyledTableCellHost(int logicalColumn, StyledTableView *view, QWidget *parent)
    : ColumnHost(logicalColumn, parent)
    , m_view(view)
{
}

void StyledTableCellHost::bindIndex(const QModelIndex &index)
{
    m_index = QPersistentModelIndex(index);
    update();
}

void StyledTableCellHost::clearIndex()
{
    m_index = QPersistentModelIndex();
    update();
}

void StyledTableCellHost::paintEvent(QPaintEvent *)
{
    if (!m_view || m_view->visualStateScope() == VirtualTableView::VisualStateScope::Row)
        return;
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    if (m_index.isValid())
        m_view->paintVisualStateBackground(&painter, QRectF(rect()), QModelIndex(m_index));
}

StyledTableRowWidget::StyledTableRowWidget(StyledTableView *view, QWidget *parent)
    : QWidget(parent)
    , m_view(view)
{
}

void StyledTableRowWidget::bindRow(const QModelIndex &index)
{
    m_index = QPersistentModelIndex(index);
    for (QWidget *child : findChildren<QWidget *>()) {
        if (auto *host = dynamic_cast<StyledTableCellHost *>(child))
            host->bindIndex(index.siblingAtColumn(host->logicalColumn()));
    }
    update();
}

void StyledTableRowWidget::unbindRow()
{
    m_index = QPersistentModelIndex();
    for (QWidget *child : findChildren<QWidget *>()) {
        if (auto *host = dynamic_cast<StyledTableCellHost *>(child))
            host->clearIndex();
    }
    update();
}

void StyledTableRowWidget::refreshVisualState(const QModelIndex &index)
{
    if (!m_view)
        return;
    if (m_view->visualStateScope() == VirtualTableView::VisualStateScope::Row) {
        update();
    } else {
        for (QWidget *child : findChildren<QWidget *>()) {
            auto *host = dynamic_cast<StyledTableCellHost *>(child);
            if (host && host->logicalColumn() == index.column() && host->isVisible()) {
                host->update();
                break;
            }
        }
    }
}

void StyledTableRowWidget::refreshVisualStates()
{
    update();
    for (QWidget *child : findChildren<QWidget *>()) {
        if (auto *host = dynamic_cast<StyledTableCellHost *>(child)) {
            if (host->isVisible())
                host->update();
        }
    }
}

void StyledTableRowWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    if (!m_view || !m_index.isValid()
        || m_view->visualStateScope() != VirtualTableView::VisualStateScope::Row)
        return;

    QRectF background(rect());
    const int radius = m_view->visualStateCornerRadius();
    if (radius > 0) {
        const qreal extension = 2.0 * radius;
        background.adjust(m_view->hasLeftRowEdge() ? 0.0 : -extension, 0.0,
                          m_view->hasRightRowEdge() ? 0.0 : extension, 0.0);
    }
    painter.setClipRect(rect());
    m_view->paintVisualStateBackground(&painter, background, QModelIndex(m_index));
}

class StyledTableView::GridCoverOverlay : public QWidget
{
public:
    explicit GridCoverOverlay(StyledTableView *view)
        : QWidget(view)
        , m_view(view)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QRegion grid;
        if (m_view->verticalGridLinesVisible()) {
            const int lineWidth = m_view->verticalGridLineWidth();
            for (int logical : m_view->visibleColumnLogicalIndexes()) {
                const ColumnGeometry column = m_view->columnGeometry(logical);
                if (column.isValid() && !column.hidden)
                    grid += QRect(column.viewportX + column.width - lineWidth, 0,
                                  lineWidth, height());
            }
        }
        if (m_view->horizontalGridLinesVisible() && m_view->rowSpacing() == 0) {
            const int lineWidth = m_view->horizontalGridLineWidth();
            for (const MaterializedItem &item : m_view->materializedItems()) {
                if (item.index.isValid() && m_view->model()
                    && item.index.row() + 1 < m_view->model()->rowCount())
                    grid += QRect(0, item.geometry.bottom() - lineWidth + 1,
                                  width(), lineWidth);
            }
        }
        for (const QRect &separator : m_view->paneSeparatorRects())
            grid += separator.translated(-pos());
        if (grid.isEmpty())
            return;

        QPainter painter(this);
        painter.setClipRegion(grid);
        const auto paintCell = [this, &painter](const QRect &clip, const QRectF &background,
                                                const QModelIndex &index) {
            const VirtualItemView::VisualState state = m_view->visualState(index);
            if (state.hoverProgress <= 0.0 && state.selectedProgress <= 0.0)
                return;
            painter.save();
            painter.setClipRect(clip, Qt::IntersectClip);
            painter.setRenderHint(QPainter::Antialiasing,
                                  m_view->visualStateCornerRadius() > 0);
            painter.fillPath(m_view->visualStateBackgroundPath(background),
                             palette().color(QPalette::Base));
            m_view->paintVisualStateBackground(&painter, background, index);
            painter.restore();
        };

        const bool rowScope = m_view->visualStateScope()
            == VirtualTableView::VisualStateScope::Row;
        const QVector<TablePane> panes = rowScope ? QVector<TablePane>() : m_view->panes();
        const QVector<int> columns = rowScope ? QVector<int>()
                                               : m_view->visibleColumnLogicalIndexes();
        for (const MaterializedItem &item : m_view->materializedItems()) {
            if (!item.index.isValid() || !item.geometry.intersects(event->rect()))
                continue;
            if (rowScope) {
                QRectF background(item.geometry);
                const int radius = m_view->visualStateCornerRadius();
                if (radius > 0) {
                    const qreal extension = 2.0 * radius;
                    background.adjust(m_view->hasLeftRowEdge() ? 0.0 : -extension, 0.0,
                                      m_view->hasRightRowEdge() ? 0.0 : extension, 0.0);
                }
                paintCell(item.geometry, background, QModelIndex(item.index));
                continue;
            }
            for (int logical : columns) {
                const ColumnGeometry column = m_view->columnGeometry(logical);
                if (!column.isValid() || column.hidden)
                    continue;
                QRect cell(column.viewportX, item.geometry.y(), column.width,
                           item.geometry.height());
                for (const TablePane &pane : panes) {
                    if (pane.logicalColumns.contains(logical)) {
                        cell = cell.intersected(pane.viewportRect);
                        break;
                    }
                }
                if (!cell.isEmpty())
                    paintCell(cell, QRectF(column.viewportX, item.geometry.y(),
                                           column.width, item.geometry.height()),
                              QModelIndex(item.index).siblingAtColumn(logical));
            }
        }
    }

private:
    StyledTableView *m_view = nullptr;
};

StyledTableView::StyledTableView(QWidget *parent)
    : VirtualTableView(parent)
{
    m_gridCover = new GridCoverOverlay(this);
    m_gridCover->setGeometry(viewport()->geometry());
    m_gridCover->hide();
    connect(this, &VirtualTableView::horizontalOffsetChanged, this, [this]() {
        refreshRoundedEdges();
        refreshGridCover();
    });
    connect(this, &VirtualTableView::columnGeometryChanged, this, [this]() {
        refreshRoundedEdges();
        refreshGridCover();
    });
    connect(this, &VirtualItemView::virtualizationUpdated, this,
            [this]() { refreshGridCover(); });
}

QPainterPath StyledTableView::visualStateBackgroundPath(const QRectF &rect) const
{
    QPainterPath path;
    if (m_cornerRadius > 0)
        path.addRoundedRect(rect, m_cornerRadius, m_cornerRadius);
    else
        path.addRect(rect);
    return path;
}

void StyledTableView::paintVisualStateBackground(QPainter *painter, const QRectF &rect,
                                                  const QModelIndex &index) const
{
    const VisualState state = visualState(index);
    if (state.hoverProgress <= 0.0 && state.selectedProgress <= 0.0)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, m_cornerRadius > 0);
    const QPainterPath path = visualStateBackgroundPath(rect);
    painter->setOpacity(state.hoverProgress);
    painter->fillPath(path, hoverBackgroundColor());
    painter->setOpacity(state.selectedProgress);
    painter->fillPath(path, selectedBackgroundColor());
    painter->restore();
}

void StyledTableView::setVisualStateCornerRadius(int radius)
{
    radius = qMax(0, radius);
    if (m_cornerRadius == radius)
        return;
    m_cornerRadius = radius;
    refreshVisualStates();
}

void StyledTableView::setBackgroundCoversGridLines(bool enabled)
{
    if (m_backgroundCoversGridLines == enabled)
        return;
    m_backgroundCoversGridLines = enabled;
    m_gridCover->setVisible(enabled);
    refreshGridCover();
}

bool StyledTableView::hasLeftRowEdge() const
{
    return !frozenColumns().isEmpty() || horizontalOffset() == 0;
}

bool StyledTableView::hasRightRowEdge() const
{
    return !frozenRightColumns().isEmpty()
        || horizontalOffset() >= maximumHorizontalOffset();
}

void StyledTableView::refreshRoundedEdges()
{
    const bool left = hasLeftRowEdge();
    const bool right = hasRightRowEdge();
    if (left == m_leftRounded && right == m_rightRounded)
        return;
    m_leftRounded = left;
    m_rightRounded = right;
    if (visualStateScope() == VisualStateScope::Row) {
        for (const MaterializedItem &item : materializedItems()) {
            if (item.widget)
                item.widget->update();
        }
    }
}

void StyledTableView::refreshVisualStates()
{
    VirtualTableView::refreshVisualStates();
    for (const MaterializedItem &item : materializedItems()) {
        if (auto *row = dynamic_cast<StyledTableRowWidget *>(item.widget))
            row->refreshVisualStates();
    }
    refreshGridCover();
}

void StyledTableView::refreshVisualState(const QModelIndex &index)
{
    VirtualTableView::refreshVisualState(index);
    if (!index.isValid())
        return;
    for (const MaterializedItem &item : materializedItems()) {
        if (item.index.isValid() && item.index.parent() == index.parent()
            && item.index.row() == index.row()) {
            if (auto *row = dynamic_cast<StyledTableRowWidget *>(item.widget))
                row->refreshVisualState(index);
            refreshGridCover(item.geometry);
            break;
        }
    }
}

void StyledTableView::resizeEvent(QResizeEvent *event)
{
    VirtualTableView::resizeEvent(event);
    refreshGridCover();
}

void StyledTableView::refreshGridCover(const QRect &dirty)
{
    if (!m_backgroundCoversGridLines)
        return;
    m_gridCover->setGeometry(viewport()->geometry());
    m_gridCover->raise();
    if (dirty.isNull())
        m_gridCover->update();
    else
        m_gridCover->update(dirty);
}

} // namespace viv
