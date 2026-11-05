#include "ui_drag_drop.h"
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

namespace tree_drag_drop {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeDragDropPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeDragDropPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(ui->view);
        for (int group = 0; group < 6; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 10; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 2; ++leaf)
                        node->appendRow(new QStandardItem(QStringLiteral("叶子 %1.%2.%3")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1)));
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
        // Its default item flags permit dragging and dropping into nodes.
        m_model->invisibleRootItem()->setDropEnabled(true);
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        ui->view->setDragEnabled(true);
        ui->view->setDragStartMode(viv::VirtualItemView::DragStartMode::Automatic);
        ui->view->setDragDropActions(Qt::CopyAction | Qt::MoveAction);
        ui->view->setDefaultDropAction(Qt::MoveAction);
        // Standard-item drops insert copies. The view removes the source after successful Move.
        // Keep this false for a custom model that already performs an actual move.
        ui->view->setMoveRemovesSourceRows(true);
        // This chapter demonstrates indicators; chapter 20 demonstrates node displacement.
        ui->view->setDragFeedbackMode(viv::VirtualTreeView::DragFeedbackMode::Indicator);
        ui->view->setDropIndicatorShown(true);
        ui->view->setDropIndicatorColor(QColor(QStringLiteral("#2676c8")));
        ui->view->setDropIndicatorWidth(2);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setDragStartMode(value == 0 ? viv::VirtualItemView::DragStartMode::Automatic
                                              : viv::VirtualItemView::DragStartMode::Manual);
        });
        connect(ui->action, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setDefaultDropAction(value == 0 ? Qt::MoveAction : Qt::CopyAction);
        });
        connect(ui->width, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setDropIndicatorWidth);
        connect(ui->dashed, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setDropIndicatorPenStyle(enabled ? Qt::DashLine : Qt::SolidLine);
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

} // namespace tree_drag_drop
