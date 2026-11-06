#include "example.h"
#include "ui_drag_drop.h"
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QToolButton>
#include <QFontMetrics>
#include <QPointer>
#include <memory>
#include <QMouseEvent>

namespace wrap_drag_drop {

class DragHandle : public QToolButton
{
public:
    explicit DragHandle(viv::VirtualWrapView *view, QWidget *parent)
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
    QPointer<viv::VirtualWrapView> m_view;
    QPoint m_press;
    bool m_pressed = false;
};

class Tag : public QLabel
{
public:
    explicit Tag(viv::VirtualWrapView *view, QWidget *parent) : QLabel(parent) { setupUi(view); }
    void setTitle(const QString &text) { m_title = text; updateText(); }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        m_handle->move(6, (height() - m_handle->height()) / 2);
        updateText();
    }
private:
    void setupUi(viv::VirtualWrapView *view)
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

class TagAdapter : public viv::WidgetAdapter
{
    QPointer<viv::VirtualWrapView> m_view;
public:
    explicit TagAdapter(viv::VirtualWrapView *view) : m_view(view) {}
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
        // Obtain width from data without creating a widget for every model row.
        return QSize(QFontMetrics(QApplication::font()).horizontalAdvance(index.data().toString()) + 48, 32);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::WrapDragDropPage)
    {
        ui->setupUi(this);
        setupExample();
    }
private:
    void setupExample()
    {
        // Paint an item base below hover and selection, leaving gaps unfilled.
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        // Model rows are item identities, independent of the packed display rows.
        model = new QStandardItemModel(0, 1, this);
        const QStringList names{QStringLiteral("Qt"), QStringLiteral("Ready"), QStringLiteral("In progress"),
                                QStringLiteral("Requires discussion"), QStringLiteral("Done")};
        for (int row = 0; row < 45; ++row)
            model->appendRow(new QStandardItem(names[row % names.size()] + QStringLiteral(" %1").arg(row + 1)));
        // The view owns the adapter. The page owns the model.
        ui->view->setAdapter(new TagAdapter(ui->view), true);
        ui->view->setFixedItemHeight(36);
        ui->view->setItemWidthRange(40, 280);
        ui->view->setContentMargins(QMargins(12, 12, 12, 12));
        ui->view->setModel(model);

        // QStandardItemModel inserts MIME copies; the framework removes source rows on Move.
        ui->view->setMoveRemovesSourceRows(true);
        ui->view->setDragDropActions(Qt::MoveAction | Qt::CopyAction);
        ui->view->setDefaultDropAction(Qt::MoveAction);
        ui->view->setDragEnabled(true);
        ui->view->setDropIndicatorColor(QColor(QStringLiteral("#d04d55")));
        ui->view->setDropIndicatorWidth(2);
        connect(ui->manual, &QCheckBox::toggled, this, [this](bool checked) {
            ui->view->setDragStartMode(checked ? viv::VirtualItemView::DragStartMode::Manual
                                             : viv::VirtualItemView::DragStartMode::Automatic);
        });
        connect(ui->dashed, &QCheckBox::toggled, this, [this](bool checked) {
            ui->view->setDropIndicatorPenStyle(checked ? Qt::DashLine : Qt::SolidLine);
        });
    }

    std::unique_ptr<Ui::WrapDragDropPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace wrap_drag_drop
