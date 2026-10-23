#include "borderdemo.h"
#include <virtualitemviews/virtualtableview.h>

namespace table_borders {

class CornerCover : public BorderDemo
{
public:
    explicit CornerCover(QWidget *parent) : BorderDemo(parent)
    {
        attachView(new viv::VirtualTableView(this));
    }

protected:
    bool coversCorners() const override { return true; }
};

BorderDemo *createCornerCover(QWidget *parent)
{
    // The sibling overlay stays above the complete table subtree without intercepting input.
    return new CornerCover(parent);
}

} // namespace table_borders
