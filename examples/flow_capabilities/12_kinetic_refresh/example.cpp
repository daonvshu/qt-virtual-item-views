#include "../../refresh_demo/page.h"
#include <virtualitemviews/virtualflowtableview.h>

QWidget *refresh_demo::flow(QWidget *parent)
{
    auto *view = new viv::VirtualFlowTableView;
    view->setCellWidthRange(110, 180);
    view->setFixedCellHeight(80);
    PageOptions options;
    options.pageSize = 64;
    options.itemLimit = 2000;
    return page(parent, view, false, options);
}
