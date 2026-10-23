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
#include <QVBoxLayout>
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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("20 树拖放：行上下边缘插入兄弟节点，中间投放为子节点；Automatic 从文字起拖，Manual 从手柄起拖。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        mode = new QComboBox(this);
        mode->addItems({QStringLiteral("Automatic"), QStringLiteral("Manual")});
        controls->addWidget(mode);
        action = new QComboBox(this);
        action->addItems({QStringLiteral("默认 Move / Ctrl Copy"), QStringLiteral("默认 Copy")});
        controls->addWidget(action);
        width = new QSpinBox(this);
        width->setRange(0, 8);
        width->setValue(2);
        width->setPrefix(QStringLiteral("指示器线宽 "));
        controls->addWidget(width);
        dashed = new QCheckBox(QStringLiteral("虚线"), this);
        dashed->setChecked(false);
        controls->addWidget(dashed);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
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
        m_view->setAdapter(new DragAdapter(*m_view), true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(40);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // QStandardItemModel implements MIME copying/insertion, including node children.
        // Its default item flags permit dragging and dropping into nodes.
        m_model->invisibleRootItem()->setDropEnabled(true);
        m_view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        m_view->setDragEnabled(true);
        m_view->setDragStartMode(viv::VirtualItemView::DragStartMode::Automatic);
        m_view->setDragDropActions(Qt::CopyAction | Qt::MoveAction);
        m_view->setDefaultDropAction(Qt::MoveAction);
        // Standard-item drops insert copies. The view removes the source after successful Move.
        // Keep this false for a custom model that already performs an actual move.
        m_view->setMoveRemovesSourceRows(true);
        m_view->setDropIndicatorShown(true);
        m_view->setDropIndicatorColor(QColor(QStringLiteral("#2676c8")));
        m_view->setDropIndicatorWidth(2);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->setDragStartMode(value == 0 ? viv::VirtualItemView::DragStartMode::Automatic
                                              : viv::VirtualItemView::DragStartMode::Manual);
        });
        connect(action, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->setDefaultDropAction(value == 0 ? Qt::MoveAction : Qt::CopyAction);
        });
        connect(width, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualItemView::setDropIndicatorWidth);
        connect(dashed, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setDropIndicatorPenStyle(enabled ? Qt::DashLine : Qt::SolidLine);
        });
        connect(m_view, &viv::VirtualItemView::itemDropped, this,
                [this](const QModelIndex &parent, int row, int, Qt::DropAction result) {
            m_status->setText(QStringLiteral("接收 parent=%1 / row=%2 / action=%3")
                .arg(parent.data().toString()).arg(row).arg(int(result)));
        });
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QComboBox *mode = nullptr;
    QComboBox *action = nullptr;
    QSpinBox *width = nullptr;
    QCheckBox *dashed = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_drag_drop
