#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <functional>

namespace list_drag_drop {

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

    void setState(const viv::VirtualItemView::VisualState &state)
    {
        m_state = state;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setOpacity(m_state.hoverProgress);
        painter.setBrush(QColor(QStringLiteral("#e4effb")));
        painter.drawRoundedRect(QRectF(rect()), 6, 6);
        painter.setOpacity(m_state.selectedProgress);
        painter.setBrush(QColor(QStringLiteral("#b8d5f3")));
        painter.drawRoundedRect(QRectF(rect()), 6, 6);
    }

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

    viv::VirtualItemView::VisualState m_state;
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
    void visualStateChanged(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<DragRow *>(widget)->setState(m_view.visualState(index));
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<DragRow *>(widget);
        row->title->clear();
        row->setState({});
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
        auto *intro = new QLabel(QStringLiteral("15 拖放：Automatic 从行内容起拖，Manual 只从手柄起拖。拖到行边缘插入，靠近视口边缘自动滚动；Esc 取消。"), this);
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
        auto *indicatorControls = new QHBoxLayout;
        m_indicatorColor = new QComboBox(this);
        m_indicatorColor->addItem(QStringLiteral("跟随调色板"), QColor());
        m_indicatorColor->addItem(QStringLiteral("蓝色"), QColor(QStringLiteral("#409eff")));
        m_indicatorColor->addItem(QStringLiteral("橙色"), QColor(QStringLiteral("#e89028")));
        m_indicatorWidth = new QSpinBox(this);
        m_indicatorWidth->setRange(0, 16);
        m_indicatorWidth->setValue(2);
        m_indicatorWidth->setSuffix(QStringLiteral(" px"));
        m_indicatorStyle = new QComboBox(this);
        m_indicatorStyle->addItem(QStringLiteral("实线"), int(Qt::SolidLine));
        m_indicatorStyle->addItem(QStringLiteral("虚线"), int(Qt::DashLine));
        m_indicatorStyle->addItem(QStringLiteral("点线"), int(Qt::DotLine));
        m_indicatorStyle->addItem(QStringLiteral("点划线"), int(Qt::DashDotLine));
        m_indicatorStyle->addItem(QStringLiteral("双点划线"), int(Qt::DashDotDotLine));
        m_indicatorStyle->addItem(QStringLiteral("不绘制"), int(Qt::NoPen));
        indicatorControls->addWidget(new QLabel(QStringLiteral("指示器颜色"), this));
        indicatorControls->addWidget(m_indicatorColor);
        indicatorControls->addWidget(new QLabel(QStringLiteral("线宽"), this));
        indicatorControls->addWidget(m_indicatorWidth);
        indicatorControls->addWidget(new QLabel(QStringLiteral("线型"), this));
        indicatorControls->addWidget(m_indicatorStyle);
        indicatorControls->addStretch();
        layout->addLayout(indicatorControls);
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
        // Keep the view-referencing adapter alive through row teardown callbacks.
        // This example paints state inside its row widget.
        m_view->setVisualStateBackgroundVisible(false);
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
        // Indicator visibility changes presentation only; model drop permissions still apply.
        m_view->setDropIndicatorShown(true);
        // An invalid QColor follows QPalette::Highlight, including later palette changes.
        m_view->setDropIndicatorColor(QColor());
        // Width is in logical pixels and applies to both lines and frames; zero hides them.
        m_view->setDropIndicatorWidth(2);
        // SolidLine is the default; DashLine/DotLine and other built-in patterns also work.
        m_view->setDropIndicatorPenStyle(Qt::SolidLine);
        m_view->setModel(model);
        connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            m_view->setDragStartMode(mode == 0 ? viv::VirtualItemView::DragStartMode::Automatic
                                              : viv::VirtualItemView::DragStartMode::Manual);
        });
        connect(m_action, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int action) {
            m_view->setDefaultDropAction(action == 0 ? Qt::MoveAction : Qt::CopyAction);
        });
        connect(m_indicatorColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            m_view->setDropIndicatorColor(m_indicatorColor->currentData().value<QColor>());
        });
        connect(m_indicatorWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int width) {
            m_view->setDropIndicatorWidth(width);
        });
        connect(m_indicatorStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            m_view->setDropIndicatorPenStyle(static_cast<Qt::PenStyle>(m_indicatorStyle->currentData().toInt()));
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
    QComboBox *m_indicatorColor = nullptr;
    QSpinBox *m_indicatorWidth = nullptr;
    QComboBox *m_indicatorStyle = nullptr;
    QLabel *m_status = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_drag_drop
