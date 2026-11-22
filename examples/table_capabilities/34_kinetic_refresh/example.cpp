#include "../../refresh_demo/page.h"
#include <virtualitemviews/virtualtableview.h>

QWidget *refresh_demo::table(QWidget *parent)
{
    auto *view = new viv::VirtualTableView;
    view->setUniformItemHeight(40);
    return page(parent, view);
}
