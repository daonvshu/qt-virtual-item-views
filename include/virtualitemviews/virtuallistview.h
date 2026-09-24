#pragma once

#include <virtualitemviews/virtualitemview.h>

class QAbstractItemModel;

namespace viv {

class ListLayout;

/// Vertically stacked, virtualized list of QWidget items.
///
/// VirtualListView is the v0.1 entry point of VirtualItemViews: it materializes
/// only the visible, overscan and pinned items of a QAbstractItemModel and
/// recycles their widgets while scrolling. Row heights are managed by the
/// kernel (ItemHeightMode, estimates and measurement) and the list only adds the
/// row <-> QModelIndex mapping below rootIndex().
class VirtualListView : public VirtualItemView
{
    Q_OBJECT

public:
    /// Item size modes live in the kernel so that the Table can reuse them.
    using ItemHeightMode = VirtualItemView::ItemHeightMode;

    explicit VirtualListView(QWidget *parent = nullptr);
    ~VirtualListView() override;

    /// Root of the list. An invalid index means the invisible root of the model.
    void setRootIndex(const QModelIndex &index);
    QModelIndex rootIndex() const { return m_rootIndex; }

    ListLayout *listLayout() const { return m_listLayout; }

protected:
    qsizetype viewItemCount() const override;
    QModelIndex viewIndex(qsizetype item, int column = 0) const override;
    qsizetype viewItemForIndex(const QModelIndex &index) const override;
    bool isLayoutParent(const QModelIndex &parent) const override;

private:
    ListLayout *m_listLayout = nullptr;
    QModelIndex m_rootIndex;
};

} // namespace viv
