#include "../../refresh_demo/page.h"
#include <virtualitemviews/virtualtreeview.h>

QWidget *refresh_demo::tree(QWidget *parent)
{
    auto *view = new viv::VirtualTreeView;
    view->setUniformItemHeight(40);
    return page(parent, view, true);
}
