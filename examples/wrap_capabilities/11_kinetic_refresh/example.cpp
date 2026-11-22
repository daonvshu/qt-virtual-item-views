#include "../../refresh_demo/page.h"
#include <virtualitemviews/virtualwrapview.h>

QWidget *refresh_demo::wrap(QWidget *parent)
{
    auto *view = new viv::VirtualWrapView;
    view->setFixedItemHeight(44);
    view->setItemWidthRange(100, 200);
    return page(parent, view);
}
