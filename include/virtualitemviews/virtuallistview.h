#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/virtualitemview.h>

#include <QColor>
#include <QHash>
#include <QPersistentModelIndex>
#include <QVector>

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

    /// Row separators are visible by default, including when row spacing is zero.
    void setRowGridLinesVisible(bool visible);
    bool rowGridLinesVisible() const { return m_rowGridLinesVisible; }
    /// Width is clamped to at least one pixel.
    void setRowGridLineWidth(int pixels);
    int rowGridLineWidth() const { return m_rowGridLineWidth; }
    /// An invalid color restores the palette's mid color.
    void setRowGridLineColor(const QColor &color);
    QColor rowGridLineColor() const { return m_rowGridLineColor; }
    /// Extends hover/selection backgrounds through row gaps (default false).
    void setHoverBackgroundThroughRowSpacing(bool enabled);
    bool hoverBackgroundThroughRowSpacing() const { return m_hoverBackgroundThroughRowSpacing; }
    void setSelectedBackgroundThroughRowSpacing(bool enabled);
    bool selectedBackgroundThroughRowSpacing() const { return m_selectedBackgroundThroughRowSpacing; }
    QColor itemPaneSeparatorColor() const override;

protected:
    qsizetype viewItemCount() const override;
    QModelIndex viewIndex(qsizetype item, int column = 0) const override;
    qsizetype viewItemForIndex(const QModelIndex &index) const override;
    bool isLayoutParent(const QModelIndex &parent) const override;
    void configureRowSpacingWidget(QWidget *widget) const override;
    void afterMaterialize() override;
    void paintEvent(QPaintEvent *event) override;
    void refreshVisualStates() override;
    void refreshVisualState(const QModelIndex &index) override;

private:
    void syncRowGridLines();

    ListLayout *m_listLayout = nullptr;
    QPersistentModelIndex m_rootIndex;
    QHash<qsizetype, QWidget *> m_rowGridLines;
    QVector<QWidget *> m_rowGridLinePool;
    QColor m_rowGridLineColor;
    int m_rowGridLineWidth = 1;
    bool m_rowGridLinesVisible = true;
    bool m_hoverBackgroundThroughRowSpacing = false;
    bool m_selectedBackgroundThroughRowSpacing = false;
};

} // namespace viv
