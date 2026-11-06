#include "example.h"
#include "ui_drag_drop.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <memory>
#include <QApplication>
#include <QMouseEvent>
#include <QToolButton>
#include <QResizeEvent>
#include <QPointer>

namespace masonry_drag_drop {

class DragHandle : public QToolButton
{
public:
    explicit DragHandle(viv::VirtualMasonryView *view, QWidget *parent)
        : QToolButton(parent), m_view(view) {}
protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && isManual()) {
            m_press = mapToGlobal(mousePosition(event));
            m_pressed = true;
            setDown(true);
            event->accept();
            return;
        }
        // Automatic mode must receive the press in the view as well as the move.
        event->ignore();
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!isManual() || !m_pressed || !(event->buttons() & Qt::LeftButton)) {
            event->ignore();
            return;
        }
        event->accept();
        if ((mapToGlobal(mousePosition(event)) - m_press).manhattanLength() < QApplication::startDragDistance())
            return;

        m_pressed = false;
        setDown(false);
        // Resolve the owning tag's current index after any widget reuse.
        const QModelIndex index = m_view->indexForWidget(parentWidget());
        if (!index.isValid())
            return;
        viv::DragOptions options;
        options.globalPressPosition = m_press;
        // The native drag may destroy this handle. Do not access it afterwards.
        m_view->startDrag(index, options);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_pressed) {
            m_pressed = false;
            setDown(false);
            event->accept();
            return;
        }
        event->ignore();
    }
private:
    bool isManual() const
    {
        return m_view && m_view->dragStartMode() == viv::VirtualItemView::DragStartMode::Manual;
    }
    static QPoint mousePosition(const QMouseEvent *event)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return event->position().toPoint();
#else
        return event->pos();
#endif
    }
    QPointer<viv::VirtualMasonryView> m_view;
    QPoint m_press;
    bool m_pressed = false;
};

class Tag : public QLabel
{
public:
    explicit Tag(viv::VirtualMasonryView *view, QWidget *parent) : QLabel(parent) { setupUi(view); }
    void setTitle(const QString &text) { m_title = text; updateText(); }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        m_handle->move(6, (height() - m_handle->height()) / 2);
        updateText();
    }
private:
    void setupUi(viv::VirtualMasonryView *view)
    {
        setMargin(10);
        // Reserve space for the handle without covering the label text.
        setIndent(24);
        m_handle = new DragHandle(view, this);
        m_handle->setText(QStringLiteral("\u22ee"));
        m_handle->setToolTip(QStringLiteral("Drag handle (Manual mode)"));
        m_handle->setFixedSize(20, 22);
        m_handle->setAutoRaise(true);
        m_handle->setFocusPolicy(Qt::NoFocus);
        m_handle->setCursor(Qt::SizeAllCursor);
    }
    void updateText() { setText(fontMetrics().elidedText(m_title, Qt::ElideRight, qMax(0, width() - 44))); }
    QString m_title;
    DragHandle *m_handle = nullptr;
};

class CardAdapter : public viv::WidgetAdapter
{
    QPointer<viv::VirtualMasonryView> m_view;
public:
    explicit CardAdapter(viv::VirtualMasonryView *view) : m_view(view) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new Tag(m_view.data(), parent);
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Always replace the complete presentation when a cached widget is rebound.
        static_cast<Tag *>(widget)->setTitle(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<Tag *>(widget)->setTitle(QString());
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        // Obtain each aspect-ratio estimate without creating offscreen cards.
        return index.data(Qt::UserRole + 1).toSize();
    }
};


class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryDragDropPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int row = 0; row < 70; ++row) {
            auto *item = new QStandardItem(QStringLiteral("Card %1").arg(row + 1));
            item->setData(QSize(160, 80 + (row * 47) % 180), Qt::UserRole + 1);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            model->appendRow(item);
        }
        // The view owns the adapter; the model lives through card teardown.
        ui->view->setAdapter(new CardAdapter(ui->view), true);
        // Estimates describe aspect ratios; columns receive independent heights.
        ui->view->setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
        ui->view->setCellWidthRange(100, 200);
        ui->view->setRowSpacing(10);
        ui->view->setColumnSpacing(10);
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        ui->view->setModel(model);

        model->invisibleRootItem()->setDropEnabled(true);
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        // Automatic uses the platform drag threshold; Manual is started by DragHandle.
        ui->view->setDragEnabled(true);
        ui->view->setDragStartMode(viv::VirtualItemView::DragStartMode::Automatic);
        ui->view->setDragDropActions(Qt::CopyAction | Qt::MoveAction);
        ui->view->setDefaultDropAction(Qt::MoveAction);
        // Standard MIME inserts copies. Delete originals only after an accepted Move.
        // A custom model that calls beginMoveRows/endMoveRows must leave this false.
        ui->view->setMoveRemovesSourceRows(true);
        ui->view->setDropIndicatorColor(QColor(QStringLiteral("#377dcc")));
        ui->view->setDropIndicatorWidth(2);
        connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setDragStartMode(mode == 0 ? viv::VirtualItemView::DragStartMode::Automatic
                                              : viv::VirtualItemView::DragStartMode::Manual);
        });
        connect(ui->action, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int action) {
            ui->view->setDefaultDropAction(action == 0 ? Qt::MoveAction : Qt::CopyAction);
        });
        connect(ui->indicator, &QCheckBox::toggled, ui->view, &viv::VirtualItemView::setDropIndicatorShown);
        connect(ui->width, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setDropIndicatorWidth);
        connect(ui->pen, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int pen) {
            const Qt::PenStyle styles[]{Qt::SolidLine, Qt::DashLine, Qt::DotLine};
            ui->view->setDropIndicatorPenStyle(styles[pen]);
        });
        connect(model, &QAbstractItemModel::rowsRemoved, this, [this]() {
            ui->status->setText(QStringLiteral("Move completed; model rows=%1").arg(model->rowCount()));
        });
        ui->status->setText(QStringLiteral("移动是单项目操作；卡片最终插入顺序由 model 的行序决定，最短列布局会重新排布。"));
    }

    std::unique_ptr<Ui::MasonryDragDropPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_drag_drop
