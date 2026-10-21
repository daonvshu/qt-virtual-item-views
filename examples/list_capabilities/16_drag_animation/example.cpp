#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <functional>

namespace list_drag_animation {

class DragHandle : public QPushButton
{
public:
    explicit DragHandle(QWidget *parent) : QPushButton(QStringLiteral("拖动手柄"), parent) {}
    std::function<void(const QPoint &)> beginDrag;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        m_pressed = event->button() == Qt::LeftButton;
        m_globalPress = mapToGlobal(event->pos());
        QPushButton::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_pressed && (event->buttons() & Qt::LeftButton) && beginDrag
            && (mapToGlobal(event->pos()) - m_globalPress).manhattanLength() >= QApplication::startDragDistance()) {
            m_pressed = false;
            setDown(false);
            event->accept();
            // The native drag enters a nested event loop; do not access this after it.
            const auto start = beginDrag;
            const QPoint press = m_globalPress;
            start(press);
            return;
        }
        QPushButton::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_pressed = false;
        QPushButton::mouseReleaseEvent(event);
    }

private:
    bool m_pressed = false;
    QPoint m_globalPress;
};

class DragRow : public QWidget
{
public:
    explicit DragRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    DragHandle *handle = nullptr;
    QLabel *title = nullptr;

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        handle = new DragHandle(this);
        title = new QLabel(this);
        layout->addWidget(handle);
        layout->addWidget(title, 1);
    }
};

class DragAdapter : public viv::WidgetAdapter
{
public:
    explicit DragAdapter(viv::VirtualListView &view) : m_view(view) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new DragRow(parent);
        row->handle->beginDrag = [this, row](const QPoint &globalPress) {
            // Automatic mode is handled by the view's mouse-distance logic.
            // The business handle starts drags only when Manual is selected.
            if (m_view.dragStartMode() != viv::VirtualItemView::DragStartMode::Manual)
                return;
            // A reused widget may now represent another item: resolve its index
            // at gesture time rather than capturing the index from bindWidget().
            const QModelIndex index = m_view.indexForWidget(row);
            if (!index.isValid())
                return;
            viv::DragOptions options;
            // The list's default preview source is already the whole row widget.
            // Preserve the original press point as the preview hotspot. The view
            // converts global coordinates and handles clipping/high-DPI capture.
            options.globalPressPosition = globalPress;
            // The view checks permissions/MIME, pins the source, suspends hover,
            // runs QDrag and cleans up on drop/cancel. No business QDrag is needed.
            m_view.startDrag(index, options);
        };
        return row;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<DragRow *>(widget)->title->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<DragRow *>(widget);
        row->title->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(500, 44); }

private:
    viv::VirtualListView &m_view;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel(QStringLiteral("16 行移动动画拖动：拖动时其他行滑动让位，放下才改变数据。Automatic 和 Manual 均支持；复制时使用默认指示器，Esc 取消。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *controls = new QHBoxLayout;
        m_mode = new QComboBox(this);
        m_mode->addItems({QStringLiteral("Automatic"), QStringLiteral("Manual")});
        m_action = new QComboBox(this);
        m_action->addItems({QStringLiteral("默认移动（Ctrl 复制）"), QStringLiteral("默认复制")});
        controls->addWidget(m_mode);
        controls->addWidget(m_action);
        layout->addLayout(controls);
        auto *animationControls = new QHBoxLayout;
        m_animationDuration = new QSpinBox(this);
        m_animationDuration->setRange(0, 2000);
        m_animationDuration->setValue(180);
        m_animationDuration->setSuffix(QStringLiteral(" ms"));
        m_animationDuration->setToolTip(QStringLiteral("0 表示立即让位，不播放过渡动画"));
        m_animationEasing = new QComboBox(this);
        m_animationEasing->addItem(QStringLiteral("OutCubic"), int(QEasingCurve::OutCubic));
        m_animationEasing->addItem(QStringLiteral("Linear"), int(QEasingCurve::Linear));
        m_animationEasing->addItem(QStringLiteral("InOutCubic"), int(QEasingCurve::InOutCubic));
        animationControls->addWidget(new QLabel(QStringLiteral("行移动时长"), this));
        animationControls->addWidget(m_animationDuration);
        animationControls->addWidget(new QLabel(QStringLiteral("动画曲线"), this));
        animationControls->addWidget(m_animationEasing);
        animationControls->addStretch();
        layout->addLayout(animationControls);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(QStringLiteral("QStandardItemModel 负责 MIME 与插入；view 在 Move 成功后删除源行，并让新条目继承原条目的选择。"), this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        auto *model = new QStandardItemModel(m_view);
        for (int row = 0; row < 100; ++row) {
            auto *item = new QStandardItem(QStringLiteral("条目 %1").arg(row + 1));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            model->appendRow(item);
        }
        model->invisibleRootItem()->setDropEnabled(true);
        auto *adapter = new DragAdapter(*m_view);
        // Use built-in hover/selection backgrounds instead of custom row painting.
        m_view->setVisualStateBackgroundVisible(true);
        m_view->setVisualStateCornerRadius(6);
        m_view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        m_view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // Keep the view-referencing adapter alive through row teardown callbacks.
        m_view->setAdapter(adapter, true);
        m_view->setUniformItemHeight(44);
        // This example drags one row. The model owns the MIME/data semantics.
        m_view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        // Enable native drag/drop processing. This does not replace model flags
        // or the model's canDropMimeData()/dropMimeData() implementation.
        m_view->setDragEnabled(true);
        // Automatic starts after the platform drag distance; Manual requires startDrag().
        m_view->setDragStartMode(viv::VirtualItemView::DragStartMode::Automatic);
        // Offered actions restrict negotiation; default action chooses the preferred
        // result when the target allows it and modifiers do not request another action.
        m_view->setDragDropActions(Qt::CopyAction | Qt::MoveAction);
        m_view->setDefaultDropAction(Qt::MoveAction);
        // QStandardItemModel inserts the payload; it does not move existing row identities.
        // Let the view delete source rows after Move. Keep false for a model that
        // already performs moveRows(), or the moved rows could be deleted again.
        // For this row-insertion model, the view also transfers selection/current
        // to the inserted counterpart after deleting the selected source row.
        m_view->setMoveRemovesSourceRows(true);
        // Neighboring rows make room during a single-row Move. The model stays
        // unchanged until drop; Automatic and Manual use the same animation.
        // Copy, external drags and other unsupported cases retain the default indicator.
        m_view->setDragFeedbackMode(viv::VirtualListView::DragFeedbackMode::RowMoveAnimation);
        // These settings control row movement, independently of hover animation.
        // Zero duration makes room immediately; the default is 180ms / OutCubic.
        m_view->setDragReorderAnimationDuration(180);
        m_view->setDragReorderAnimationEasingCurve(QEasingCurve::OutCubic);
        m_view->setModel(model);
        connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            m_view->setDragStartMode(mode == 0 ? viv::VirtualItemView::DragStartMode::Automatic
                                              : viv::VirtualItemView::DragStartMode::Manual);
        });
        connect(m_action, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int action) {
            m_view->setDefaultDropAction(action == 0 ? Qt::MoveAction : Qt::CopyAction);
        });
        connect(m_animationDuration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int duration) {
            m_view->setDragReorderAnimationDuration(duration);
        });
        connect(m_animationEasing, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            m_view->setDragReorderAnimationEasingCurve(
                QEasingCurve(static_cast<QEasingCurve::Type>(m_animationEasing->currentData().toInt())));
        });
        // Emitted only after the model accepts a drop. Source cleanup may still
        // follow when the native drag returns, so this is not a final-state snapshot.
        connect(m_view, &viv::VirtualItemView::itemDropped, this,
                [this](const QModelIndex &, int row, int, Qt::DropAction action) {
            m_status->setText(QStringLiteral("model 已接受落点：插入 row=%1，action=%2；拖动结束后框架恢复普通 hover。")
                .arg(row).arg(action == Qt::MoveAction ? QStringLiteral("Move") : QStringLiteral("Copy")));
        });
    }

    viv::VirtualListView *m_view = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_action = nullptr;
    QSpinBox *m_animationDuration = nullptr;
    QComboBox *m_animationEasing = nullptr;
    QLabel *m_status = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_drag_animation
