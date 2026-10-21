#pragma once

#include <QStandardItemModel>

namespace reference {

// Column zero owns children, as required by Qt's tree model contract.
// The same three-column model also serves the tree-table examples.
inline void populateTree(QStandardItemModel &model)
{
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Kind"),
                                    QStringLiteral("State")});
    for (int group = 0; group < 4; ++group) {
        auto *parent = new QStandardItem(QStringLiteral("Group %1").arg(group));
        model.appendRow({parent, new QStandardItem(QStringLiteral("Group")),
                         new QStandardItem(QStringLiteral("Ready"))});
        for (int row = 0; row < 8; ++row)
            parent->appendRow({new QStandardItem(QStringLiteral("Record %1").arg(row)),
                               new QStandardItem(QStringLiteral("Record")),
                               new QStandardItem(QStringLiteral("Ready"))});
    }
}

} // namespace reference
