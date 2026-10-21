#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/virtualitemview.h>

#include <QColor>
#include <QHash>
#include <QPersistentModelIndex>
#include <QVector>
#include <memory>

class QAbstractItemModel;

namespace viv {

class ListLayout;
class RowDragPreview;

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

    /// Native drag feedback; model order changes only after an accepted drop.
    enum class DragFeedbackMode { Indicator, RowMoveAnimation };
    Q_ENUM(DragFeedbackMode)
    /// Default RowMoveAnimation. Animation supports an internal single-row Move within
    /// the current root's scrolling rows; other drops retain the indicator.
    void setDragFeedbackMode(DragFeedbackMode mode);
    DragFeedbackMode dragFeedbackMode() const { return m_dragFeedbackMode; }
    /// Duration in milliseconds (default 180); zero makes room immediately.
    void setDragReorderAnimationDuration(int milliseconds);
    int dragReorderAnimationDuration() const { return m_dragReorderAnimationDuration; }
    /// Curve for rows making room (default OutCubic).
    void setDragReorderAnimationEasingCurve(const QEasingCurve &curve);
    QEasingCurve dragReorderAnimationEasingCurve() const { return m_dragReorderEasingCurve; }

    /// Row separators are visible by default, including when row spacing is zero.
    void setRowGridLinesVisible(bool visible);
    bool rowGridLinesVisible() const { return m_rowGridLinesVisible; }
    /// Width is clamped to at least one pixel.
    void setRowGridLineWidth(int pixels);
    int rowGridLineWidth() const { return m_rowGridLineWidth; }
    /// An invalid color restores the palette's mid color.
    void setRowGridLineColor(const QColor &color);
    QColor rowGridLineColor() const { return m_rowGridLineColor; }
    /// Built-in hover/selection backgrounds are enabled by default; widgets stay transparent.
    void setVisualStateBackgroundVisible(bool visible);
    bool visualStateBackgroundVisible() const { return m_visualStateBackgroundVisible; }
    /// Radius in logical pixels, clamped to zero; item spacing remains unpainted.
    void setVisualStateCornerRadius(int radius);
    int visualStateCornerRadius() const { return m_visualStateCornerRadius; }
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
    bool updateDragFeedback(const DropTarget &target, const QPoint &position, Qt::DropAction action,
                            const QList<QPersistentModelIndex> &sources) override;
    void clearDragFeedback() override;

private:
    void syncRowGridLines();
    /// Display geometry used by row widgets, backgrounds and separators alike.
    QRect visualRowRect(qsizetype row) const;
    /// Samples elapsed time even between animation ticks during fast dragging.
    qreal dragReorderProgress() const;
    /// Updates existing widgets and spacing without rebinding business content.
    void applyDragReorderFrame();

    ListLayout *m_listLayout = nullptr;
    QPersistentModelIndex m_rootIndex;
    QHash<qsizetype, QWidget *> m_rowGridLines;
    QVector<QWidget *> m_rowGridLinePool;
    QColor m_rowGridLineColor;
    int m_rowGridLineWidth = 1;
    bool m_rowGridLinesVisible = true;
    bool m_visualStateBackgroundVisible = true;
    int m_visualStateCornerRadius = 0;
    DragFeedbackMode m_dragFeedbackMode = DragFeedbackMode::RowMoveAnimation;
    int m_dragReorderAnimationDuration = 180;
    QEasingCurve m_dragReorderEasingCurve = QEasingCurve::OutCubic;
    std::unique_ptr<RowDragPreview> m_dragRowPreview;
    QPersistentModelIndex m_dragPreviewSource;
    QVariantAnimation *m_dragReorderAnimation = nullptr;
    QElapsedTimer m_dragReorderClock;
    QVector<QMetaObject::Connection> m_dragModelConnections;
    quint64 m_dragPreviewSerial = 0;
    bool m_dragReorderFrameActive = false;
};

} // namespace viv
