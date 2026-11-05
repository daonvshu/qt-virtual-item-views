#include "ui_item_events.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <functional>
#include <utility>

namespace list_item_events {

class EventRow : public QWidget
{
public:
    explicit EventRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    QLabel *title = nullptr;
    QPushButton *rename = nullptr;

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        title = new QLabel(this);
        rename = new QPushButton(QStringLiteral("行内事件"), this);
        layout->addWidget(title, 1);
        layout->addWidget(rename);
    }
};

class EventAdapter : public viv::WidgetAdapter
{
public:
    EventAdapter(viv::VirtualListView &view, std::function<void(const QString &)> report)
        : m_view(view), m_report(std::move(report)) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new EventRow(parent);
        QObject::connect(row->rename, &QPushButton::clicked, row, [this, row] {
            // Never capture an index passed to a previous bind.
            const QModelIndex index = m_view.indexForWidget(row);
            if (!index.isValid() || !m_view.model())
                return;
            m_report(QStringLiteral("行内事件 -> 当前 index.row=%1，数据=%2")
                .arg(index.row()).arg(index.data().toString()));
            m_view.model()->setData(index, index.data().toString() + QStringLiteral(" *"));
        });
        return row;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<EventRow *>(widget)->title->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<EventRow *>(widget)->title->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(500, 40); }

private:
    viv::VirtualListView &m_view;
    std::function<void(const QString &)> m_report;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListItemEventsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListItemEventsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
        ui->log->setMaximumBlockCount(100);
    }

    void setupExample()
    {
        auto *model = new QStandardItemModel(ui->view);
        for (int row = 0; row < 200; ++row)
            model->appendRow(new QStandardItem(QStringLiteral("条目 %1").arg(row + 1)));
        // The adapter resolves a reused row's current index when its button is clicked.
        ui->view->setAdapter(new EventAdapter(*ui->view, [this](const QString &text) {
            ui->log->appendPlainText(text);
        }), true);
        ui->view->setUniformItemHeight(40);
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        ui->view->setModel(model);
        // Row signals report model indexes. A child button keeps its own clicked
        // signal, handled separately by EventAdapter rather than as a row click.
        connect(ui->view, &viv::VirtualItemView::clicked, this, [this](const QModelIndex &index) {
            report(QStringLiteral("clicked"), index);
        });
        connect(ui->view, &viv::VirtualItemView::doubleClicked, this, [this](const QModelIndex &index) {
            report(QStringLiteral("doubleClicked"), index);
        });
        connect(ui->view, &viv::VirtualItemView::activated, this, [this](const QModelIndex &index) {
            report(QStringLiteral("activated"), index);
        });
        // Obtain selectionModel() after assigning the model; keyboard navigation
        // also changes the current index and reaches this connection.
        connect(ui->view->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex &index) { report(QStringLiteral("currentChanged"), index); });
        connect(ui->view->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this] { reportSelection(); });
        connect(ui->locate, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            report(QStringLiteral("当前项"), index);
            reportSelection();
            // visualRect is in viewport coordinates; indexAt performs the reverse lookup.
            const QRect rect = ui->view->visualRect(index);
            const bool visible = rect.intersects(ui->view->viewport()->rect());
            const auto hit = visible ? ui->view->indexAt(rect.intersected(ui->view->viewport()->rect()).center()) : QModelIndex();
            ui->log->appendPlainText(QStringLiteral("widget=%1；rect=(%2,%3 %4×%5)；hit.row=%6")
                // Lookup does not create a widget: an unmaterialized row returns null.
                // Offscreen rows retained by pinning may still have a widget.
                .arg(ui->view->widgetForIndex(index) ? QStringLiteral("已物化") : QStringLiteral("未物化"))
                .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()).arg(hit.row()));
        });
    }

    void reportSelection()
    {
        // Selection belongs to model indexes, even when their widgets are recycled.
        // The current index alone does not describe which items are selected.
        const QModelIndexList selected = ui->view->selectionModel()->selectedIndexes();
        ui->log->appendPlainText(QStringLiteral("选中项：共 %1 项").arg(selected.size()));
        for (const QModelIndex &index : selected)
            report(QStringLiteral("选中项"), index);
    }

    void report(const QString &event, const QModelIndex &index)
    {
        ui->log->appendPlainText(QStringLiteral("%1 -> row=%2，数据=%3")
            .arg(event).arg(index.row()).arg(index.data().toString()));
    }

};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_item_events
