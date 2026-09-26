#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/types.h>

#include <QtGlobal>

class QAbstractItemModel;
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

    /// Tells the adapter which model the labels come from.
    ///
    /// Called by VirtualHeaderView::setLabelModel() (with nullptr when the header drops its
    /// model), so an adapter that captures the model - the README example does - does not
    /// keep a second, stale copy of it: the header owns the model reference, the adapter is
    /// told what it is. The default does nothing: an adapter that reads the model through
    /// its own reference keeps working.
    virtual void setLabelModel(QAbstractItemModel *model) { Q_UNUSED(model); }

    /// Fills \a widget with the data of \a logicalIndex; called before the widget
    /// is shown and every time the section widget is reused. It is also called
    /// again for a widget that is *already* bound when the state of its section
    /// changes (a renamed column, a column inserted/removed/moved, a new sort
    /// indicator), so the section UI must be rebuildable from scratch here - that
    /// is the only "state changed" hook a renderer gets.
    virtual void bindSection(QWidget *widget, int logicalIndex) = 0;

    /// Detaches \a widget from \a logicalIndex (stop timers, animations,
    /// subscriptions; clear the content). Animation state must never leak into
    /// the section the widget is bound to next (§19). This is a *recycle* hook:
    /// it is called when the widget leaves the materialized set, not before a
    /// re-bind of the same section.
    virtual void unbindSection(QWidget *widget, int logicalIndex)
    {
        Q_UNUSED(widget);
        Q_UNUSED(logicalIndex);
    }
};

} // namespace viv
