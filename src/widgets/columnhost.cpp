#include <virtualitemviews/tablewidgetadapter.h>

namespace viv {

ColumnHost::ColumnHost(int logicalColumn, QWidget *parent)
    : QWidget(parent)
    , m_logicalColumn(logicalColumn)
{
}

void ColumnHost::setLogicalColumn(int logicalColumn)
{
    if (m_logicalColumn == logicalColumn)
        return;
    m_logicalColumn = logicalColumn;
    updateGeometry();
}

} // namespace viv
