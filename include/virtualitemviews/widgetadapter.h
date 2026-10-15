#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/types.h>

#include <QModelIndex>
#include <QSize>
#include <QWidget>

namespace viv {

/// Bridge between the virtualization kernel and the business widgets.
///
/// The adapter never paints: it owns the QWidget lifecycle of an item. It must
/// be able to bind the same widget to a different QModelIndex many times, and
/// unbindWidget() must stop every activity that is tied to the old index
/// (QTimer, animation, QMovie, async image requests, business subscriptions).
class VIRTUALITEMVIEWS_EXPORT WidgetAdapter
{
public:
    virtual ~WidgetAdapter() = default;

    /// Classifies an index into a recycler pool. The default keeps a single pool.
    virtual WidgetType widgetType(const QModelIndex &index) const
    {
        Q_UNUSED(index);
        return kDefaultWidgetType;
    }

    /// Creates a new widget for \a type; \a parent is the view viewport.
    virtual QWidget *createWidget(WidgetType type, QWidget *parent) = 0;

    /// Fills \a widget with the data of \a index. Called before the widget is
    /// shown, so implementations must not rely on being visible yet. The type
    /// is the recycler type used to create this widget, not a new classification.
    ///
    /// Kept as the compatibility entry point. New adapters can override the
    /// ItemBindingContext overload below and receive the complete row/node
    /// payload directly.
    virtual void bindWidget(WidgetType type, QWidget *widget, const QModelIndex &index)
    {
        Q_UNUSED(type);
        Q_UNUSED(widget);
        Q_UNUSED(index);
    }

    /// Fills \a widget with the current row/node payload and binding metadata.
    /// The default delegates to the index-only callback for existing adapters.
    virtual void bindWidget(WidgetType type, QWidget *widget, const ItemBindingContext &context)
    {
        bindWidget(type, widget, context.index);
    }

    /// Called after binding and when hover or selection changes. The widget can
    /// query the view's visualState() and paint its own background.
    virtual void visualStateChanged(WidgetType type, QWidget *widget, const QModelIndex &index)
    {
        Q_UNUSED(type);
        Q_UNUSED(index);
        widget->update();
    }

    /// Context-aware visual-state callback. Existing adapters continue to receive
    /// the index-only callback through the default implementation.
    virtual void visualStateChanged(WidgetType type, QWidget *widget, const ItemBindingContext &context)
    {
        visualStateChanged(type, widget, context.index);
    }

    /// Detaches \a widget from \a index. Called before the widget is hidden and
    /// recycled, and before a widget is rebound to another index.
    virtual void unbindWidget(WidgetType type, QWidget *widget, const QModelIndex &index)
    {
        Q_UNUSED(type);
        Q_UNUSED(widget);
        Q_UNUSED(index);
    }

    /// Context-aware lifecycle callback. Existing adapters continue to receive
    /// the index-only callback through the default implementation.
    virtual void unbindWidget(WidgetType type, QWidget *widget, const ItemBindingContext &context)
    {
        unbindWidget(type, widget, context.index);
    }

    /// Estimated size of an index before it is materialized. Used for newly
    /// inserted rows in variable-height layouts. The type is widgetType(index).
    virtual QSize estimatedSize(WidgetType type, const QModelIndex &index) const = 0;
};

} // namespace viv

