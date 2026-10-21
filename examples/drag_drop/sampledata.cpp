#include "dragmodels.h"

namespace dragdemo {

QStandardItemModel *buildTreeModel(QObject *parent)
{
    auto *model = new QStandardItemModel(parent);
    const QStringList lines{QStringLiteral("SMT-1"), QStringLiteral("SMT-2"),
                            QStringLiteral("AOI-1"), QStringLiteral("PACK-1")};
    for (const QString &line : lines) {
        auto *node = new QStandardItem(line);
        node->setFlags(dndFlags());
        node->setData(QStringLiteral("产线"), Qt::UserRole + 1);
        for (int station = 0; station < 3; ++station) {
            auto *child = new QStandardItem(QStringLiteral("%1-工位%2").arg(line).arg(station + 1));
            child->setFlags(dndFlags());
            child->setData(QStringLiteral("工位"), Qt::UserRole + 1);
            if (station == 0) {
                for (int device = 0; device < 2; ++device) {
                    auto *leaf = new QStandardItem(
                        QStringLiteral("%1-设备%2").arg(line).arg(device + 1));
                    leaf->setFlags(dndFlags());
                    leaf->setData(QStringLiteral("设备"), Qt::UserRole + 1);
                    child->appendRow(leaf);
                }
            }
            node->appendRow(child);
        }
        model->appendRow(node);
    }
    return model;
}

DraggableTableModel *buildTableModel(int rows, int columns, QObject *parent)
{
    auto *model = new DraggableTableModel(rows, columns, parent);
    QStringList labels;
    for (int column = 0; column < columns; ++column)
        labels << QStringLiteral("列 %1").arg(column + 1);
    model->setHorizontalHeaderLabels(labels);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            // 留出空白格，直接演示单元格移动和 Ctrl 复制。
            if ((row + column) % 5 == 4)
                continue;
            auto *item = new QStandardItem(QStringLiteral("r%1c%2").arg(row).arg(column + 1));
            item->setFlags(dndFlags());
            model->setItem(row, column, item);
        }
    }
    return model;
}

} // namespace dragdemo
