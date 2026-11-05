#include "ui_drag_animation.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableDragAnimationPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableDragAnimationPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        ui->view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_adapter = new CellAdapter(*ui->view);
        ui->view->setCellAdapter(m_adapter, true);

        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(80, 8, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // SelectRows collects whole rows for MIME; the model must allow dragging and root-level insertion.
        ui->view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        ui->view->setDragEnabled(true);
        ui->view->setDragDropActions(Qt::CopyAction | Qt::MoveAction);
        ui->view->setDefaultDropAction(Qt::MoveAction);
        // QStandardItemModel inserts copied data; after a successful Move, the view removes source rows and transfers selection.
        // Disable this option if the model already moves its data through moveRows.
        ui->view->setMoveRemovesSourceRows(true);
        // Change drag feedback without changing model data or drop semantics.
        ui->view->setDragFeedbackMode(viv::VirtualTableView::DragFeedbackMode::RowMoveAnimation);

        connect(ui->start, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setDragStartMode(value == 0 ? viv::VirtualItemView::DragStartMode::Automatic : viv::VirtualItemView::DragStartMode::Manual);
        });
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setDragReorderAnimationDuration(value);
        });
        connect(ui->curve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            const QEasingCurve::Type curves[] = {QEasingCurve::OutCubic, QEasingCurve::InOutCubic, QEasingCurve::Linear};
            ui->view->setDragReorderAnimationEasingCurve(QEasingCurve(curves[value]));
        });
    }

    QStandardItemModel *m_model = nullptr;
    CellAdapter *m_adapter = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_drag_animation
