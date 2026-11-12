#pragma once

#include <QVariant>
#include <QWidget>

namespace viv {
namespace detail {

// Layout-only hosts must not acquire decoration from application-wide QSS.
// Scope the rule to marked hosts so business children retain their own styling.
inline void prepareTransparentHost(QWidget *widget)
{
    widget->setProperty("_vivTransparentHost", true);
    widget->setStyleSheet(QStringLiteral(
        "QWidget[_vivTransparentHost=\"true\"] {"
        " background: transparent; background-image: none;"
        " border: none; border-image: none; border-radius: 0px;"
        " margin: 0px; padding: 0px;"
        " }"));
    widget->setAutoFillBackground(false);
    widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
    widget->setAttribute(Qt::WA_NoSystemBackground, true);
}

} // namespace detail
} // namespace viv
