#include "ui_drag_drop.h"
#include <memory>
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListDragDropPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListDragDropPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
        ui->indicatorColor->setItemData(0, QColor());
        ui->indicatorColor->setItemData(1, QColor(QStringLiteral("#409eff")));
        ui->indicatorColor->setItemData(2, QColor(QStringLiteral("#e89028")));
        ui->indicatorStyle->setItemData(0, int(Qt::SolidLine));
        ui->indicatorStyle->setItemData(1, int(Qt::DashLine));
        ui->indicatorStyle->setItemData(2, int(Qt::DotLine));
        ui->indicatorStyle->setItemData(3, int(Qt::DashDotLine));
        ui->indicatorStyle->setItemData(4, int(Qt::DashDotDotLine));
        ui->indicatorStyle->setItemData(5, int(Qt::NoPen));
    }

    void setupExample()
    {
        auto *model = new QStandardItemModel(ui->view);
        for (int row = 0; row < 100; ++row) {
            auto *item = new QStandardItem(QStringLiteral("条目 %1").arg(row + 1));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            model->appendRow(item);
        }
        model->invisibleRootItem()->setDropEnabled(true);
        auto *adapter = new DragAdapter(*ui->view);
        // Use built-in hover/selection backgrounds instead of custom row painting.
        ui->view->setVisualStateBackgroundVisible(true);
        ui->view->setVisualStateCornerRadius(6);
        ui->view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        ui->view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // Keep the view-referencing adapter alive through row teardown callbacks.
        ui->view->setAdapter(adapter, true);
        ui->view->setUniformItemHeight(44);
        // This example drags one row. The model owns the MIME/data semantics.
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        // Enable native drag/drop processing. This does not replace model flags
        // or the model's canDropMimeData()/dropMimeData() implementation.
        ui->view->setDragEnabled(true);
        // Automatic starts after the platform drag distance; Manual requires startDrag().
        ui->view->setDragStartMode(viv::VirtualItemView::DragStartMode::Automatic);
        // Offered actions restrict negotiation; default action chooses the preferred
        // result when the target allows it and modifiers do not request another action.
        ui->view->setDragDropActions(Qt::CopyAction | Qt::MoveAction);
        ui->view->setDefaultDropAction(Qt::MoveAction);
        // QStandardItemModel inserts the payload; it does not move existing row identities.
        // Let the view delete source rows after Move. Keep false for a model that
        // already performs moveRows(), or the moved rows could be deleted again.
        // For this row-insertion model, the view also transfers selection/current
        // to the inserted counterpart after deleting the selected source row.
        ui->view->setMoveRemovesSourceRows(true);
        // Keep this chapter on insertion indicators; the next chapter shows row animation.
        ui->view->setDragFeedbackMode(viv::VirtualListView::DragFeedbackMode::Indicator);
        // Indicator visibility changes presentation only; model drop permissions still apply.
        ui->view->setDropIndicatorShown(true);
        // An invalid QColor follows QPalette::Highlight, including later palette changes.
        ui->view->setDropIndicatorColor(QColor());
        // Width is in logical pixels and applies to both lines and frames; zero hides them.
        ui->view->setDropIndicatorWidth(2);
        // SolidLine is the default; DashLine/DotLine and other built-in patterns also work.
        ui->view->setDropIndicatorPenStyle(Qt::SolidLine);
        ui->view->setModel(model);
        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setDragStartMode(mode == 0 ? viv::VirtualItemView::DragStartMode::Automatic
                                              : viv::VirtualItemView::DragStartMode::Manual);
        });
        connect(ui->action, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int action) {
            ui->view->setDefaultDropAction(action == 0 ? Qt::MoveAction : Qt::CopyAction);
        });
        connect(ui->indicatorColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            ui->view->setDropIndicatorColor(ui->indicatorColor->currentData().value<QColor>());
        });
        connect(ui->indicatorWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int width) {
            ui->view->setDropIndicatorWidth(width);
        });
        connect(ui->indicatorStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            ui->view->setDropIndicatorPenStyle(static_cast<Qt::PenStyle>(ui->indicatorStyle->currentData().toInt()));
        });
        // Emitted only after the model accepts a drop. Source cleanup may still
        // follow when the native drag returns, so this is not a final-state snapshot.
        connect(ui->view, &viv::VirtualItemView::itemDropped, this,
                [this](const QModelIndex &, int row, int, Qt::DropAction action) {
            ui->status->setText(QStringLiteral("model 已接受落点：插入 row=%1，action=%2；拖动结束后框架恢复普通 hover。")
                .arg(row).arg(action == Qt::MoveAction ? QStringLiteral("Move") : QStringLiteral("Copy")));
        });
    }

};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_drag_drop
