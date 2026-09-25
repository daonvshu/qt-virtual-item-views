#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/types.h>

#include <QModelIndex>
#include <QSize>

class QWidget;

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
    /// shown, so implementations must not rely on being visible yet.
    virtual void bindWidget(QWidget *widget, const QModelIndex &index) = 0;

    /// Detaches \a widget from \a index. Called before the widget is hidden and
    /// recycled, and before a widget is rebound to another index.
    virtual void unbindWidget(QWidget *widget, const QModelIndex &index)
    {
        Q_UNUSED(widget);
        Q_UNUSED(index);
    }

    /// Estimated size of an index before it is materialized. Used for newly
    /// inserted rows in variable-height layouts.
    virtual QSize estimatedSize(const QModelIndex &index) const = 0;
};

} // namespace viv

