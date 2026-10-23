#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QApplication>
#include <functional>

namespace table_drag_animation {

class DragHandle : public QPushButton
{
public:
    explicit DragHandle(QWidget *parent) : QPushButton(QStringLiteral("拖动"), parent) { setFocusPolicy(Qt::NoFocus); }
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
            // QDrag runs a nested event loop; do not access the handle after invoking the callback.
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
class CellAdapter : public viv::CellWidgetAdapter
{
public:
    explicit CellAdapter(viv::VirtualTableView &view) : m_view(view) {}
    viv::WidgetType cellWidgetType(const QModelIndex &index) const override { return index.column() == 0 ? 1 : 0; }
    QWidget *createCellWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == 1) {
            auto *handle = new DragHandle(parent);
            handle->beginDrag = [this, handle](const QPoint &globalPress) {
                if (m_view.dragStartMode() != viv::VirtualItemView::DragStartMode::Manual) return;
                // Query the current index when the gesture occurs, because this widget may have been reused.
                const QModelIndex index = m_view.cellIndexForWidget(handle);
                if (!index.isValid()) return;
                viv::DragOptions options;
                options.globalPressPosition = globalPress;
                // The framework handles permissions, MIME, hover suspension, pinning, hotspots and cleanup.
                m_view.startDrag(index, options);
            };
            return handle;
        }
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        if (index.column() == 0)
            static_cast<DragHandle *>(widget)->setText(QStringLiteral("拖动 %1").arg(index.row() + 1));
        else
            static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        if (auto *handle = qobject_cast<QPushButton *>(widget))
            handle->setText(QString());
        else
            static_cast<QLabel *>(widget)->clear();
    }
private:
    viv::VirtualTableView &m_view;
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
        auto *description = new QLabel(QStringLiteral("25 整行移动动画拖动：整行 Move 与 Ctrl 复制，支持自动起拖或手柄手动起拖"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_start = new QComboBox(this);
        m_start->addItems({QStringLiteral("Automatic 自动起拖"), QStringLiteral("Manual 手柄起拖")});
        controls->addWidget(m_start);
        m_duration = new QSpinBox(this);
        m_duration->setRange(0, 800);
        m_duration->setValue(180);
        m_duration->setPrefix(QStringLiteral("让位 ms "));
        controls->addWidget(m_duration);
        m_curve = new QComboBox(this);
        m_curve->addItems({QStringLiteral("OutCubic"), QStringLiteral("InOutCubic"), QStringLiteral("Linear")});
        controls->addWidget(m_curve);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        m_view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_adapter = new CellAdapter(*m_view);
        m_view->setCellAdapter(m_adapter, true);

        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(80, 8, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);
        // SelectRows collects whole rows for MIME; the model must allow dragging and root-level insertion.
        m_view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);
        m_view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        m_view->setDragEnabled(true);
        m_view->setDragDropActions(Qt::CopyAction | Qt::MoveAction);
        m_view->setDefaultDropAction(Qt::MoveAction);
        // QStandardItemModel inserts copied data; after a successful Move, the view removes source rows and transfers selection.
        // Disable this option if the model already moves its data through moveRows.
        m_view->setMoveRemovesSourceRows(true);
        // Change drag feedback without changing model data or drop semantics.
        m_view->setDragFeedbackMode(viv::VirtualTableView::DragFeedbackMode::RowMoveAnimation);

        connect(m_start, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->setDragStartMode(value == 0 ? viv::VirtualItemView::DragStartMode::Automatic : viv::VirtualItemView::DragStartMode::Manual);
        });
        connect(m_duration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_view->setDragReorderAnimationDuration(value);
        });
        connect(m_curve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            const QEasingCurve::Type curves[] = {QEasingCurve::OutCubic, QEasingCurve::InOutCubic, QEasingCurve::Linear};
            m_view->setDragReorderAnimationEasingCurve(QEasingCurve(curves[value]));
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QComboBox *m_start = nullptr;
    QSpinBox *m_duration = nullptr;
    QComboBox *m_curve = nullptr;
    CellAdapter *m_adapter = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_drag_animation
