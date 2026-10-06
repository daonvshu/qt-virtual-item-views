#pragma once

#include <QModelIndex>
#include <QPair>

namespace viv {

// Split the actual gap into its upper node's below role and lower node's above role.
inline QPair<int, int> nodeBackgroundSpacing(const QModelIndex &node,
                                            const QModelIndex &previous, int belowRole,
                                            int fallback, int gapBefore, int gapAfter)
{
    const auto below = [belowRole, fallback](const QModelIndex &index) {
        bool valid = false;
        const int value = index.data(belowRole).toInt(&valid);
        return valid && value >= 0 ? value : fallback;
    };
    return {previous.isValid() ? qMax(0, gapBefore - below(previous)) : 0,
            qMin(gapAfter, below(node))};
}

} // namespace viv
