// Installed-package Masonry check: uses only the public header and exported target.
#include <virtualitemviews/virtualmasonryview.h>
#include <virtualitemviews/widgetadapter.h>

#include <QApplication>
#include <QLabel>
#include <QStandardItemModel>
#include <cstdio>

namespace {

class Adapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new QLabel(parent); }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(160, 100); }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool condition, const char *message) {
        std::printf("[%s] %s\n", condition ? "ok" : "FAIL", message);
        if (!condition)
            ++failures;
    };

    QStandardItemModel model;
    for (int row = 0; row < 200; ++row)
        model.appendRow(new QStandardItem(QStringLiteral("item %1").arg(row)));
    Adapter adapter;
    viv::VirtualMasonryView view;
    view.setAdapter(&adapter);
    view.setCellWidthRange(100, 200);
    view.setColumnSpacing(8);
    view.setRowSpacing(10);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setModel(&model);
    view.resize(700, 500);
    view.show();
    QApplication::processEvents();
    view.flushPendingRelayout();

    const QModelIndex identity = model.index(3, 0);
    check(view.columnCount() >= 3, "installed Masonry column range API");
    check(view.itemColumn(identity) >= 0, "installed Masonry source column identity");
    check(!view.visualRect(identity).isEmpty(), "installed Masonry geometry API");
    check(view.indexAt(view.visualRect(identity).center()) == identity,
          "installed Masonry hit testing API");
    view.setItemHeight(identity, 180);
    check(view.visualRect(identity).height() == 180, "installed Masonry authoritative height API");
    view.setItemHeightAnimationDuration(0);
    view.setItemHeightAnimationEnabled(true);
    view.setSelectedBackgroundThroughRowSpacing(true);
    view.setSelectedBackgroundThroughColumnSpacing(false);
    check(view.itemHeightAnimationDuration() == 0
              && view.selectedBackgroundThroughRowSpacing()
              && !view.selectedBackgroundThroughColumnSpacing(),
          "installed Masonry state and animation APIs");
    std::printf("MASONRY_CONSUMER_RESULT %s failures=%d\n",
                 failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
