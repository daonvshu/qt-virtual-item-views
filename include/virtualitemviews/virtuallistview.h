#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/virtualitemview.h>

#include <QPersistentModelIndex>

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
class VIRTUALITEMVIEWS_EXPORT VirtualListView : public VirtualItemView
{
    Q_OBJECT

public:
    /// Item size modes live in the kernel so that the Table can reuse them.
    using ItemHeightMode = VirtualItemView::ItemHeightMode;

    explicit VirtualListView(QWidget *parent = nullptr);
    ~VirtualListView() override;

    /// Switching the model drops the root: it names an item of the previous model.
    void setModel(QAbstractItemModel *model) override;

    /// Root of the list. An invalid index means the invisible root of the model.
    ///
    /// The root is a *persistent* index, so it keeps naming the same item when
    /// rows are inserted, removed or moved around it; an index of another model
    /// (or a valid index while the view has no model) is rejected with a warning.
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
    QPersistentModelIndex m_rootIndex;
};

} // namespace viv
