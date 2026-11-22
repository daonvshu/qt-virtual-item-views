#include "../../refresh_demo/page.h"
#include <virtualitemviews/virtuallistview.h>

QWidget *refresh_demo::list(QWidget *parent)
{
    auto *view = new viv::VirtualListView;
    view->setUniformItemHeight(42);
    return page(parent, view);
}
