// Handle events, current row identity and preview setup are separate responsibilities.
#include "draghandle.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>

class HandleRow : public QWidget
{
public:
    explicit HandleRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    reference::DragHandle *handle = nullptr;
    QLabel *title = nullptr;

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        handle = new reference::DragHandle(this);
        title = new QLabel(this);
        layout->addWidget(handle);
        layout->addWidget(title, 1);
    }
};

class HandleAdapter : public viv::WidgetAdapter
{
public:
    explicit HandleAdapter(viv::VirtualListView &view) : m_view(view) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new HandleRow(parent);
        row->handle->beginDrag = [this, row](const QPoint &globalPress) {
            const QModelIndex current = m_view.indexForWidget(row);
            if (!current.isValid())
                return;
            viv::DragOptions options;
            options.previewWidget = m_view.viewport();
            options.previewRect = m_view.visualRect(current);
            options.globalPressPosition = globalPress;
            m_view.startDrag(current, options);
        };
        return row;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<HandleRow *>(widget)->title->setText(index.data().toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<HandleRow *>(widget)->title->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(320, 40);
    }

private:
    viv::VirtualListView &m_view;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardItemModel model;
    for (int row = 0; row < 100; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("Record %1").arg(row)));
    viv::VirtualListView view;
    view.setAdapter(new HandleAdapter(view), true);
    view.setUniformItemHeight(40);
    view.setModel(&model);
    view.setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
    view.setDragEnabled(true);
    view.setDragStartMode(viv::VirtualItemView::DragStartMode::Manual);
    view.setDragDropActions(Qt::CopyAction | Qt::MoveAction);
    view.setDefaultDropAction(Qt::MoveAction);
    view.setMoveRemovesSourceRows(true);
    view.setWindowTitle(QStringLiteral("Manual drag: use the handle"));
    view.resize(520, 360);
    view.show();
    return app.exec();
}
