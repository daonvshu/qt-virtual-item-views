#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/types.h>

#include <QtGlobal>

class QWidget;

namespace viv {

/// Adapter of the section widgets of a VirtualHeaderView (architecture document
/// §18). Same contract as WidgetAdapter: the recycler creates/destroys widgets,
/// a pooled widget keeps no identity, and bindSection() has to be able to run
/// repeatedly on the same widget.
class VIRTUALITEMVIEWS_EXPORT HeaderWidgetAdapter
{
public:
    virtual ~HeaderWidgetAdapter() = default;

    /// Classifies a section into a recycler pool (one pool by default).
    virtual WidgetType sectionType(int logicalIndex) const
    {
        Q_UNUSED(logicalIndex);
        return kDefaultWidgetType;
    }

    virtual QWidget *createSection(WidgetType type, QWidget *parent) = 0;

    /// Fills \a widget with the data of \a logicalIndex; called before the widget
    /// is shown and every time the section widget is reused.
    virtual void bindSection(QWidget *widget, int logicalIndex) = 0;

    /// Detaches \a widget from \a logicalIndex (stop timers, animations,
    /// subscriptions; clear the content). Animation state must never leak into
    /// the section the widget is bound to next (§19).
    virtual void unbindSection(QWidget *widget, int logicalIndex)
    {
        Q_UNUSED(widget);
        Q_UNUSED(logicalIndex);
    }
};

} // namespace viv
