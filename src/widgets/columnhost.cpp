#include <virtualitemviews/tablewidgetadapter.h>
#include "../core/transparenthost_p.h"

namespace viv {

ColumnHost::ColumnHost(int logicalColumn, QWidget *parent)
    : QWidget(parent)
    , m_logicalColumn(logicalColumn)
{
    setObjectName(QStringLiteral("vivColumnHost"));
    detail::prepareTransparentHost(this);
}

void ColumnHost::setLogicalColumn(int logicalColumn)
{
    if (m_logicalColumn == logicalColumn)
        return;
    m_logicalColumn = logicalColumn;
    updateGeometry();
}

} // namespace viv
