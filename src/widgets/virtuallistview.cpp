#include <virtualitemviews/virtuallistview.h>

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/listlayout.h>

#include <QAbstractItemModel>
#include <QLoggingCategory>
#include <QPalette>

namespace viv {

VirtualListView::VirtualListView(QWidget *parent)
    : VirtualItemView(parent)
{
    m_listLayout = new ListLayout(Qt::Vertical);
    setLayoutPolicy(m_listLayout, true);
}

VirtualListView::~VirtualListView()
{
    qDeleteAll(m_rowGridLines);
    qDeleteAll(m_rowGridLinePool);
}

void VirtualListView::setRowGridLinesVisible(bool visible)
{
    if (m_rowGridLinesVisible == visible)
        return;
    m_rowGridLinesVisible = visible;
    relayout();
}

void VirtualListView::setRowGridLineWidth(int pixels)
{
    const int width = qMax(1, pixels);
    if (m_rowGridLineWidth == width)
        return;
    m_rowGridLineWidth = width;
    relayout();
}

void VirtualListView::setRowGridLineColor(const QColor &color)
{
    if (m_rowGridLineColor == color)
        return;
    m_rowGridLineColor = color;
    relayout();
}

QColor VirtualListView::itemPaneSeparatorColor() const
{
    return m_rowGridLineColor.isValid() ? m_rowGridLineColor
                                        : VirtualItemView::itemPaneSeparatorColor();
}

void VirtualListView::configureRowSpacingWidget(QWidget *widget) const
{
    widget->setProperty("vivShowSpacingLines", m_rowGridLinesVisible);
    widget->setProperty("vivSpacingLineWidth", m_rowGridLineWidth);
}

void VirtualListView::afterMaterialize()
{
    VirtualItemView::afterMaterialize();
    syncRowGridLines();
}

void VirtualListView::syncRowGridLines()
{
    QHash<qsizetype, QRect> desired;
    const QRect viewportRect = viewport()->geometry();
    if (m_rowGridLinesVisible && rowSpacing() == 0 && viewportRect.width() > 0) {
        for (const VisibleRange &range : visibleItemRanges()) {
            for (qsizetype row = range.first; row >= 0 && row <= range.last; ++row) {
                if (row + 1 >= viewItemCount())
                    continue;
                const QRect rowRect = geometryForViewRow(row);
                if (!rowRect.intersects(viewport()->rect()))
                    continue;
                const QRect lineRect(viewportRect.x(),
                                     viewportRect.y() + rowRect.bottom() - m_rowGridLineWidth + 1,
                                     viewportRect.width(), m_rowGridLineWidth);
                const QRect paneRect = itemPaneRect(itemPaneForRow(row))
                    .translated(viewportRect.topLeft());
                const QRect clipped = lineRect.intersected(paneRect);
                if (!clipped.isEmpty())
                    desired.insert(row, clipped);
            }
        }
    }
    for (auto it = m_rowGridLines.begin(); it != m_rowGridLines.end();) {
        if (desired.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->hide();
        m_rowGridLinePool.append(it.value());
        it = m_rowGridLines.erase(it);
    }
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        QWidget *line = m_rowGridLines.value(it.key(), nullptr);
        if (!line) {
            line = m_rowGridLinePool.isEmpty() ? new QWidget(this) : m_rowGridLinePool.takeLast();
            line->setObjectName(QStringLiteral("vivListRowGridLine"));
            line->setAttribute(Qt::WA_TransparentForMouseEvents);
            line->setAutoFillBackground(true);
            m_rowGridLines.insert(it.key(), line);
        }
        QPalette colors = line->palette();
        colors.setColor(QPalette::Window, itemPaneSeparatorColor());
        line->setPalette(colors);
        line->setGeometry(it.value());
        line->show();
        line->raise();
    }
}

// ---------------------------------------------------------------------------
// Identity mapping
// ---------------------------------------------------------------------------

qsizetype VirtualListView::viewItemCount() const
{
    QAbstractItemModel *m = model();
    return m ? qMax<qsizetype>(0, m->rowCount(m_rootIndex)) : 0;
}

QModelIndex VirtualListView::viewIndex(qsizetype item, int column) const
{
    QAbstractItemModel *m = model();
    if (!m || item < 0 || item >= qsizetype(m->rowCount(m_rootIndex)))
        return QModelIndex();
    return m->index(int(item), column, m_rootIndex);
}

qsizetype VirtualListView::viewItemForIndex(const QModelIndex &index) const
{
    if (!index.isValid() || !model())
        return -1;
    if (index.parent() != m_rootIndex)
        return -1;
    if (index.row() < 0 || index.row() >= model()->rowCount(m_rootIndex))
        return -1;
    return index.row();
}

bool VirtualListView::isLayoutParent(const QModelIndex &parent) const
{
    return parent == m_rootIndex;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void VirtualListView::setModel(QAbstractItemModel *model)
{
    // The root names an item of the previous model; keeping it would hand a
    // foreign parent to the new model's rowCount().
    m_rootIndex = QModelIndex();
    VirtualItemView::setModel(model);
}

void VirtualListView::setRootIndex(const QModelIndex &index)
{
    if (index == m_rootIndex)
        return;
    if (index.isValid() && index.model() != model()) {
        qWarning("VirtualListView::setRootIndex(): the index belongs to another model "
                 "(or the view has no model); the root is unchanged");
        return;
    }

    m_rootIndex = index;
    // Every identity mapping changed: rebuild the materialized set.
    recycleAllItems();
    resetLayoutForNewModel();
    relayout();
}

} // namespace viv
