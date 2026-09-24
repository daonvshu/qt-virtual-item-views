#include <virtualitemviews/virtuallistview.h>

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/sizeindex.h>
#include <virtualitemviews/listlayout.h>

#include <QAbstractItemModel>
#include <QLoggingCategory>

namespace viv {

VirtualListView::VirtualListView(QWidget *parent)
    : VirtualItemView(parent)
{
    m_listLayout = new ListLayout(Qt::Vertical);
    setLayoutPolicy(m_listLayout, true);
}

VirtualListView::~VirtualListView() = default;

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

void VirtualListView::setRootIndex(const QModelIndex &index)
{
    if (index == m_rootIndex)
        return;
    if (index.isValid() && model() && index.model() != model())
        return;

    m_rootIndex = index;
    // Every identity mapping changed: rebuild the materialized set.
    recycleAllItems();
    resetLayoutForNewModel();
    relayout();
}

} // namespace viv
