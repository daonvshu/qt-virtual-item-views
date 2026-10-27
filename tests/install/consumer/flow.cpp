// Installed-package Flow check: only public headers and public API are used.
#include <virtualitemviews/virtualflowtableview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QStandardItemModel>
#include <cstdio>

namespace {
class CardAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setAlignment(Qt::AlignCenter);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        return label;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(150, 100); }
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool condition, const char *message) {
        std::printf("[%s] %s\n", condition ? "ok" : "FAIL", message);
        if (!condition) ++failures;
    };
    QStandardItemModel model;
    for (int i = 0; i < 1000; ++i)
        model.appendRow(new QStandardItem(QStringLiteral("项目 %1").arg(i)));
    CardAdapter adapter;
    viv::VirtualFlowTableView view;
    view.setAdapter(&adapter);
    view.setModel(&model);
    view.setCellWidthRange(100, 200);
    view.setFixedCellHeight(100);
    view.resize(800, 600);
    view.show();
    QApplication::processEvents();
    view.flushPendingRelayout();
    check(view.minimumCellWidth() == 100 && view.maximumCellWidth() == 200,
          "installed Flow width API and documentation adapter");
    check(view.fixedCellHeight() == 100 && view.cellHeightMode()
              == viv::VirtualFlowTableView::CellHeightMode::Fixed, "fixed height configuration");
    const QPersistentModelIndex identity(model.index(1, 0));
    QWidget *widget = view.widgetForIndex(identity);
    check(widget && static_cast<QLabel *>(widget)->text() == QStringLiteral("项目 1"),
          "public adapter binds source column zero");
    view.setItemPinned(identity);
    view.setCurrentIndex(identity);
    const int oldColumns = view.columnCount();
    int notifications = 0;
    QObject::connect(&view, &viv::VirtualFlowTableView::flowLayoutChanged, &view, [&]() { ++notifications; });
    view.setRowSpacing(7);
    view.setColumnSpacing(9);
    view.setContentMargins(QMargins(3, 4, 5, 6));
    view.resize(420, 600);
    QApplication::processEvents();
    view.flushPendingRelayout();
    check(view.columnCount() != oldColumns && notifications > 0, "resize and exported layout signal");
    check(view.widgetForIndex(identity) == widget && view.currentIndex() == identity,
          "public reflow preserves pinned source identity");
    int width = (view.columnCount() - 1) * view.columnSpacing();
    int expectedX = view.contentMargins().left();
    for (int column = 0; column < view.columnCount(); ++column) {
        check(view.columnViewportX(column) == expectedX, "public column position");
        expectedX += view.columnWidth(column) + view.columnSpacing();
        width += view.columnWidth(column);
    }
    check(width == view.viewport()->width() - 8, "columns consume all usable width");
    check(view.displayRowCount() == (1000 + view.columnCount() - 1) / view.columnCount(),
          "display row count");
    check(view.displayRowForIndex(identity) == 1 / view.columnCount()
              && view.displayColumnForIndex(identity) == 1 % view.columnCount(), "display coordinates");
    view.setCellAspectRatio(1.5);
    view.setCellHeightMode(viv::VirtualFlowTableView::CellHeightMode::AspectRatio);
    view.flushPendingRelayout();
    check(view.cellAspectRatio() == 1.5 && view.visualRect(identity).height()
              == qRound(view.visualRect(identity).width() / 1.5), "proportional height configuration");
    view.setVisualStateBackgroundVisible(false);
    view.setStateBackgroundCornerRadius(8);
    check(!view.visualStateBackgroundVisible() && view.stateBackgroundCornerRadius() == 8,
          "exported state background APIs");
    view.setVerticalGridLinesVisible(false);
    view.setHorizontalGridLinesVisible(false);
    view.setVerticalGridLineWidth(2);
    view.setHorizontalGridLineWidth(3);
    view.setVerticalGridLineColor(Qt::red);
    view.setHorizontalGridLineColor(Qt::blue);
    view.setGridLinePlacement(viv::GridLinePlacement::SpacingEdges);
    view.setVerticalSpacingLineThroughRowSpacing(false);
    view.setHorizontalSpacingLineThroughColumnSpacing(false);
    check(!view.verticalGridLinesVisible() && !view.horizontalGridLinesVisible()
              && view.verticalGridLineWidth() == 2 && view.horizontalGridLineWidth() == 3
              && view.verticalGridLineColor() == QColor(Qt::red) && view.horizontalGridLineColor() == QColor(Qt::blue)
              && !view.verticalSpacingLineThroughRowSpacing() && !view.horizontalSpacingLineThroughColumnSpacing(),
          "exported separator APIs");
    check(view.stats().logicalItems == 1000 && view.stats().materializedItems < 100,
          "installed consumer materializes a bounded window");
    view.setItemPinned(identity, false);
    std::printf("FLOW_CONSUMER_RESULT %s failures=%d\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
