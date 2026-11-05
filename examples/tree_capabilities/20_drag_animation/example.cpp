#include "ui_drag_animation.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QApplication>
#include <QMouseEvent>
#include <functional>

namespace tree_drag_animation {

class DragHandle : public QPushButton
{
public:
    explicit DragHandle(QWidget *parent) : QPushButton(QStringLiteral("拖动手柄"), parent)
    {
        setFocusPolicy(Qt::NoFocus);
    }
    std::function<void(const QPoint &)> beginDrag;
protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        m_pressed = event->button() == Qt::LeftButton;
        m_press = mapToGlobal(event->pos());
        QPushButton::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_pressed && (event->buttons() & Qt::LeftButton) && beginDrag
            && (mapToGlobal(event->pos()) - m_press).manhattanLength() >= QApplication::startDragDistance()) {
            m_pressed = false;
            setDown(false);
            event->accept();
            // QDrag runs a nested loop. Copy arguments and do not touch this afterward.
            const auto start = beginDrag;
            const QPoint press = m_press;
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
    QPoint m_press;
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
        layout->setContentsMargins(8, 2, 8, 2);
        title = new QLabel(this);
        handle = new DragHandle(this);
        layout->addWidget(title, 1);
        layout->addWidget(handle);
    }
};
class DragAdapter : public viv::WidgetAdapter
{
public:
    explicit DragAdapter(viv::VirtualTreeView &view) : m_view(view) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new DragRow(parent);
        row->handle->beginDrag = [this, row](const QPoint &press) {
            if (m_view.dragStartMode() != viv::VirtualItemView::DragStartMode::Manual)
                return;
            const QModelIndex index = m_view.indexForWidget(row);
            if (!index.isValid())
                return;
            viv::DragOptions options;
            options.globalPressPosition = press;
            // The view captures the row and manages pinning, MIME, hover suspension and cleanup.
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
        static_cast<DragRow *>(widget)->title->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(480, 40); }
private:
    viv::VirtualTreeView &m_view;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeDragAnimationPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeDragAnimationPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
        ui->easing->setItemData(0, int(QEasingCurve::OutCubic));
        ui->easing->setItemData(1, int(QEasingCurve::Linear));
        ui->easing->setItemData(2, int(QEasingCurve::InOutCubic));
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(ui->view);
        for (int group = 0; group < 6; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 4; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row == 0) {
                    for (int leaf = 0; leaf < 2; ++leaf) {
                        auto *item = new QStandardItem(QStringLiteral("叶子 %1.%2.%3")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1));
                        // Leaves accept sibling insertion, not becoming a drop parent.
                        item->setDropEnabled(false);
                        node->appendRow(item);
                    }
                } else {
                    node->setDropEnabled(false);
                }
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new DragAdapter(*ui->view), true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(40);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // QStandardItemModel implements MIME copying/insertion, including node children.
        // Branch flags permit dropping into nodes; leaf flags allow sibling insertion only.
        m_model->invisibleRootItem()->setDropEnabled(true);
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        ui->view->setDragEnabled(true);
        ui->view->setDragStartMode(viv::VirtualItemView::DragStartMode::Automatic);
        ui->view->setDragDropActions(Qt::CopyAction | Qt::MoveAction);
        ui->view->setDefaultDropAction(Qt::MoveAction);
        // Standard-item drops insert copies. The view removes the source after successful Move.
        // Keep this false for a custom model that already performs an actual move.
        ui->view->setMoveRemovesSourceRows(true);
        // Single-node drags collapse the source immediately, before preview capture.
        // Internal Move uses one-row space; the model still moves the entire subtree.
        // Copy, external/multiple sources and dropping into nodes retain indicators.
        ui->view->setDragFeedbackMode(viv::VirtualTreeView::DragFeedbackMode::RowMoveAnimation);
        // These settings affect displacement, independently of hover and expansion.
        ui->view->setDragReorderAnimationDuration(180);
        ui->view->setDragReorderAnimationEasingCurve(QEasingCurve::OutCubic);
        ui->view->setDropIndicatorShown(true);
        ui->view->setDropIndicatorColor(QColor(QStringLiteral("#2676c8")));
        ui->view->setDropIndicatorWidth(2);

        // Populate the initial visible hierarchy without a startup expansion transition.
        ui->view->setExpansionAnimationEnabled(false);
        ui->view->expand(m_model->index(0, 0));
        ui->view->expand(m_model->index(0, 0, m_model->index(0, 0)));
        ui->view->expand(m_model->index(1, 0));
        ui->view->setExpansionAnimationEnabled(true);

        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setDragStartMode(value == 0 ? viv::VirtualItemView::DragStartMode::Automatic
                                              : viv::VirtualItemView::DragStartMode::Manual);
        });
        connect(ui->action, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setDefaultDropAction(value == 0 ? Qt::MoveAction : Qt::CopyAction);
        });
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged),
                ui->view, &viv::VirtualTreeView::setDragReorderAnimationDuration);
        connect(ui->easing, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            ui->view->setDragReorderAnimationEasingCurve(
                QEasingCurve(static_cast<QEasingCurve::Type>(ui->easing->currentData().toInt())));
        });
        connect(ui->spacing, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setRowSpacing(enabled ? 6 : 0);
        });
        connect(ui->view, &viv::VirtualItemView::itemDropped, this,
                [this](const QModelIndex &parent, int row, int, Qt::DropAction result) {
            ui->status->setText(QStringLiteral("接收 parent=%1 / row=%2 / action=%3")
                .arg(parent.data().toString()).arg(row).arg(int(result)));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_drag_animation
