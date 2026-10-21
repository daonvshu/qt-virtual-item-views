#pragma once

#include <QStandardItemModel>

namespace reference {

// Only sample data. No view, adapter, layout or interaction configuration here.
inline void populateTable(QStandardItemModel &model, int rows = 100, int columns = 3)
{
    model.setColumnCount(columns);
    model.setRowCount(rows);
    for (int column = 0; column < columns; ++column) {
        model.setHeaderData(column, Qt::Horizontal, QStringLiteral("Column %1").arg(column));
        for (int row = 0; row < rows; ++row)
            model.setData(model.index(row, column), QStringLiteral("%1 / %2").arg(row).arg(column));
    }
}

} // namespace reference
