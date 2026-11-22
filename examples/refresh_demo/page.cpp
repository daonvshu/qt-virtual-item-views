#include "page.h"
#include "ui_refreshpage.h"
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
#include <smoothscroll/smoothscrollcontroller.h>
#endif
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/widgetadapter.h>
#include <QDebug>
#include <QStandardItemModel>
#include <QTimer>

namespace refresh_demo {
namespace {
class Labels : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        return QSize(110 + index.row() % 4 * 25, 65 + index.row() % 5 * 22);
    }
};

class Cells : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
};

class Page : public QWidget
{
public:
    Page(QWidget *parent, viv::VirtualItemView *view, bool tree, const PageOptions &options)
        : QWidget(parent), view(view), model(new QStandardItemModel(this)), options(options), tree(tree)
    {
        using View = viv::VirtualItemView;
        ui.setupUi(this);
        ui.body->insertWidget(2, view, 1);
        if (auto *tableView = qobject_cast<viv::VirtualTableView *>(view)) {
            model->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("页码")});
            tableView->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
            tableView->setCellAdapter(new Cells, true);
            tableView->setDefaultColumnWidth(200);
        } else {
            model->setColumnCount(1);
            view->setAdapter(new Labels, true);
        }
        view->setModel(model);
        const auto changeMode = [view](int index) {
            const auto mode = index == 0 ? View::IndicatorMode::Inline : View::IndicatorMode::Overlay;
            view->setIndicatorModes(mode, mode);
        };
        changeMode(ui.presentation->currentIndex());
        connect(ui.presentation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changeMode);
        view->setIndicatorHeights(28, 112);
#ifdef VIV_DEMO_USE_SMOOTH_SCROLLBAR
        auto *controller = new sscroll::SmoothScrollController(view, view);
        controller->setEnabled(ui.kinetic->isChecked());
        connect(ui.kinetic, &QCheckBox::toggled, controller, &sscroll::SmoothScrollController::setEnabled);
#else
        ui.kinetic->hide();
#endif
        const auto updateEdges = [this](Qt::Edge) {
            QStringList names;
            const auto edges = this->view->scrollEdges();
            if (edges & Qt::TopEdge) names.append(QStringLiteral("顶部"));
            if (edges & Qt::BottomEdge) names.append(QStringLiteral("底部"));
            if (edges & Qt::LeftEdge) names.append(QStringLiteral("左端"));
            if (edges & Qt::RightEdge) names.append(QStringLiteral("右端"));
            ui.edge->setText(names.isEmpty() ? QStringLiteral("滚动区域中部") : names.join(QStringLiteral(" · ")));
        };
        connect(view, &View::scrollEdgeReached, this, updateEdges);
        connect(view, &View::scrollEdgeLeft, this, updateEdges);
        updateEdges(Qt::TopEdge);
        connect(view, &View::refreshRequested, this, [this](quint64 id) { fetch(id, true); });
        connect(view, &View::loadMoreRequested, this, [this](quint64 id) { fetch(id, false); });
        view->setPullToRefreshEnabled(true);
        view->setLoadMoreEnabled(true);
    }

private:
    void appendPage()
    {
        const int first = model->rowCount();
        const int limit = first + qMin(options.pageSize, options.itemLimit - first);
        for (int row = first; row < limit; ++row) {
            auto *name = new QStandardItem(QStringLiteral("%1 · %2").arg(edition).arg(row + 1));
            QList<QStandardItem *> fields{name};
            if (model->columnCount() > 1)
                fields.append(new QStandardItem(QString::number(row / options.pageSize + 1)));
            if (tree) {
                for (int child = 1; child <= 3; ++child) {
                    QList<QStandardItem *> children{new QStandardItem(QStringLiteral("子项 %1.%2").arg(row + 1).arg(child))};
                    if (fields.size() > 1)
                        children.append(new QStandardItem(QString::number(row / options.pageSize + 1)));
                    name->appendRow(children);
                }
            }
            model->appendRow(fields);
        }
    }

    void showLoadStatus(bool knownEnd)
    {
        const int count = model->rowCount();
        ui.status->setText(knownEnd ? QStringLiteral("已加载 %1 项，已到末尾").arg(count)
                                    : QStringLiteral("已加载 %1 项").arg(count));
    }

    void fetch(quint64 id, bool refresh)
    {
        qInfo().noquote() << "[refresh_demo]" << view->metaObject()->className()
                          << (refresh ? "refreshRequested" : "loadMoreRequested")
                          << "requestId=" << id << "rows=" << model->rowCount();
        ui.status->setText(refresh ? QStringLiteral("正在刷新") : QStringLiteral("正在加载下一页"));
        QTimer::singleShot(500, this, [this, id, refresh] {
            if (refresh) {
                model->removeRows(0, model->rowCount());
                ++edition;
            }
            const int first = model->rowCount();
            appendPage();
            const int count = model->rowCount();
            const bool knownEnd = ui.probe->isChecked() ? first == count : count == options.itemLimit;
            showLoadStatus(knownEnd);
            if (refresh) view->finishRefresh(id, !knownEnd);
            else if (ui.probe->isChecked()) view->finishLoadMore(id);
            else view->finishLoadMore(id, !knownEnd);
        });
    }

    Ui::RefreshPage ui;
    viv::VirtualItemView *view;
    QStandardItemModel *model;
    PageOptions options;
    bool tree;
    int edition = 1;
};
}

QWidget *page(QWidget *parent, viv::VirtualItemView *view, bool tree, const PageOptions &options)
{
    return new Page(parent, view, tree, options);
}
}
