#include "../../refresh_demo/page.h"
#include <virtualitemviews/virtualmasonryview.h>

QWidget *refresh_demo::masonry(QWidget *parent)
{
    auto *view = new viv::VirtualMasonryView;
    view->setCellWidthRange(110, 180);
    view->setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    return page(parent, view);
}
