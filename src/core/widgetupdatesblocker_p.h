#pragma once

#include <QPointer>
#include <QWidget>

namespace viv { namespace detail {

// Keep synchronous repaints from adapter callbacks inside a widget update batch
// from exposing partially recycled, rebound or positioned widgets. Nested scopes leave
// restoration to the outer scope; caller-disabled updates remain disabled.
class WidgetUpdatesBlocker
{
public:
    explicit WidgetUpdatesBlocker(QWidget *widget)
        : m_widget(widget), m_restoreUpdates(widget && widget->updatesEnabled())
    {
        if (m_restoreUpdates)
            widget->setUpdatesEnabled(false);
    }

    ~WidgetUpdatesBlocker()
    {
        if (m_restoreUpdates && m_widget)
            m_widget->setUpdatesEnabled(true);
    }

    WidgetUpdatesBlocker(const WidgetUpdatesBlocker &) = delete;
    WidgetUpdatesBlocker &operator=(const WidgetUpdatesBlocker &) = delete;

private:
    QPointer<QWidget> m_widget;
    bool m_restoreUpdates;
};

}} // namespace viv::detail
