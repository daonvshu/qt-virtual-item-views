#pragma once

#include <QDataStream>

namespace viv {

/// Use the existing state stream version when available, or the Qt 5.12 baseline.
constexpr int stateDataStreamVersion()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    return QDataStream::Qt_5_15;
#else
    return QDataStream::Qt_5_12;
#endif
}

} // namespace viv
