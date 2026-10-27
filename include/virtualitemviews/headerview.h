#pragma once

#include <virtualitemviews/global.h>

#include <QPoint>
#include <QVector>
#include <QWidget>

#include <QAbstractItemModel>
#include <QColor>
#include <QFont>
#include <QMargins>
#include <functional>

namespace viv {

class HeaderGeometry;
class HeaderWidgetAdapter;

/// Colour the current style paints a header section separator with (probed by rendering a
/// small section and reading its edge pixel). The pane boundary lines and the built-in
/// section widgets use it, so every line that separates two sections - in either axis and
/// in the body - is the style's line instead of a guessed palette role.
///
/// \a context supplies the style and the palette (pass the view or a widget in it).
VIRTUALITEMVIEWS_EXPORT QColor headerSectionSeparatorColor(const QWidget *context);

/// Renderer abstraction of a table header (architecture document §15/§16/§17).
///
/// The default implementation is VirtualHeaderView with its built-in LabelHeaderAdapter.
/// Applications can replace its adapter or install a custom renderer implementing this
/// interface.
///
/// Every renderer consumes the same HeaderGeometry, so the table body never depends on how
/// the header is rendered. The interface is intentionally not a QWidget: an implementation
/// may be one (VirtualHeaderView is), but the table only talks to this contract.
class VIRTUALITEMVIEWS_EXPORT HeaderViewInterface {
public:
    virtual ~HeaderViewInterface() = default;

    /// Binds the header to its state owner.
    virtual void setGeometryModel(HeaderGeometry *geometry) = 0;
    virtual HeaderGeometry *geometryModel() const = 0;

    /// Widget to place in the table's header area.
    virtual QWidget *headerWidget() = 0;
    /// Const access without requiring existing custom renderers to add an overload.
    virtual const QWidget *headerWidget() const
    {
        return const_cast<HeaderViewInterface *>(this)->headerWidget();
    }
    virtual Qt::Orientation orientation() const = 0;
    /// Replaces the section adapter; nullptr disables section widgets.
    /// With takeOwnership, the header deletes the adapter after releasing its sections.
    virtual void setAdapter(HeaderWidgetAdapter *adapter, bool takeOwnership = false) = 0;
    /// Current section adapter, owned by the header only when explicitly requested.
    virtual HeaderWidgetAdapter *adapter() const = 0;
    /// Base panel visibility. Built-in headers retain text, separators and state layers.
    /// Custom renderers may ignore the optional appearance settings below.
    virtual void setBackgroundVisible(bool visible) { Q_UNUSED(visible); }
    virtual bool backgroundVisible() const { return true; }
    /// Flat base color; invalid restores the native panel in built-in headers.
    virtual void setBackgroundColor(const QColor &color) { Q_UNUSED(color); }
    virtual QColor backgroundColor() const { return QColor(); }
    /// Gap color; invalid follows this header's base background.
    virtual void setSpacingBackgroundColor(const QColor &color) { Q_UNUSED(color); }
    virtual QColor spacingBackgroundColor() const { return QColor(); }
    /// Default text color; invalid inherits the widget palette's ButtonText brush.
    /// A section's model ForegroundRole takes precedence.
    virtual void setForegroundColor(const QColor &color) { Q_UNUSED(color); }
    virtual QColor foregroundColor() const { return QColor(); }
    /// Default label alignment, centered by default. Model TextAlignmentRole takes precedence.
    virtual void setTextAlignment(Qt::Alignment alignment) { Q_UNUSED(alignment); }
    virtual Qt::Alignment textAlignment() const { return Qt::AlignCenter; }
    /// Additional content insets, excluding backgrounds and separators. Zero by default.
    virtual void setContentPadding(const QMargins &padding) { Q_UNUSED(padding); }
    virtual QMargins contentPadding() const { return QMargins(); }
    /// Default widget font, inherited by sections unless their model supplies FontRole.
    /// An unresolved QFont restores inheritance from the header's parent widget.
    virtual void setFont(const QFont &font) { headerWidget()->setFont(font); }
    virtual const QFont &font() const { return headerWidget()->font(); }
    /// Model that provides the section labels (headerData()).
    virtual void setLabelModel(QAbstractItemModel *model) = 0;
    /// Enables sort interaction; the header reports clicks by writing the sort
    /// indicator into HeaderGeometry, and the table reacts to that change.
    virtual void setSortInteractionEnabled(bool enabled) = 0;

    /// Tells the renderer where the viewport starts inside the view, so a widget
    /// based header can derive its own coordinates from HeaderGeometry (which is
    /// expressed in viewport coordinates). The native adapter does not need it.
    virtual void setViewportOrigin(const QPoint &origin) { Q_UNUSED(origin); }

    /// Restricts the header to one pane (§31): only \a logicalColumns are shown,
    /// read from the same geometry (a pane is never a width copy). When
    /// \a frozen the header ignores the horizontal offset. Renderers that cannot
    /// split sections may ignore this and keep showing every section.
    virtual void setPaneFilter(const QVector<int> &logicalColumns, bool frozen)
    {
        Q_UNUSED(logicalColumns);
        Q_UNUSED(frozen);
    }
    /// Shows every section again (no pane filter).
    virtual void clearPaneFilter() {}

    /// Horizontal offset of the pane's own content (§43 "advanced panes").
    ///
    /// A pane renderer packs the columns of its own pane from the pane's left
    /// edge and shifts them by \a offset: 0 keeps a frozen pane pinned, the scroll
    /// group's offset places a scrolling pane that is not the primary one, and
    /// kFollowGeometryOffset (the default) keeps the renderer on HeaderGeometry's
    /// committed position plus its viewport offset - which is what a whole-table
    /// header and the primary scrolling pane use.
    static constexpr qint64 kFollowGeometryOffset = -1;
    virtual void setPaneOffset(qint64 offset) { Q_UNUSED(offset); }

    /// Visual geometry animation of a section move (§23/§24).
    ///
    /// A renderer that places its own sections can slide a moved section to its
    /// committed position instead of teleporting. VirtualTableView's body widgets
    /// follow visual positions according to its column and row following settings,
    /// both enabled by default; geometry queries use the committed HeaderGeometry.
    /// A renderer that cannot do this - the native QHeaderView adapter, whose
    /// placement and painting belong to Qt - ignores both calls and keeps its
    /// immediate behaviour; the header and the body then simply agree at once.
    virtual void setSectionAnimationEnabled(bool enabled) { Q_UNUSED(enabled); }
    /// Duration of the visual transition; 0 means "no animation".
    virtual void setSectionAnimationDuration(int ms) { Q_UNUSED(ms); }
    /// Tells the renderer whether the *next* committed order change is one it should
    /// show as a transition (§23/§24).
    ///
    /// A committed order change is applied immediately by default: code that reorders
    /// columns (or the model does it) gets the new order at once, never an animation
    /// nobody asked for. The view sets this around an explicitly animated move
    /// (`VirtualTableView::moveColumn(..., Animate)`), and a renderer that drives a
    /// gesture itself - the widget header's drag - sets it after its single commit.
    /// The flag is one-shot: the renderer consumes it with the next relayout.
    virtual void setSectionMoveAnimated(bool animated) { Q_UNUSED(animated); }

    // -- visual section geometry (§23/§24) ------------------------------------
    /// Where a section is *drawn* while the renderer is showing a position other
    /// than the committed one: the dragged section of a preview, or a section
    /// sliding to the order that was just committed.
    ///
    /// HeaderGeometry stays the single source of truth - the body, the scroll bar,
    /// hit testing and every query read it and only it - so a renderer that moves
    /// its own sections has to be able to *report* the visual position. That is
    /// what these three hooks are for, and they are what lets a view put the body's
    /// column widgets on the same frame instead of jumping at the commit
    /// (VirtualTableView::setColumnFollowsHeaderVisual()).
    ///
    /// Fills \a viewportX with the x the section is drawn at, in viewport
    /// coordinates (the space of ColumnGeometry::viewportX), and returns true.
    /// Returns false - "ask the committed geometry" - for a section that is hidden,
    /// not materialized, or when the renderer has no visual state at all (idle, or
    /// a renderer whose placement belongs to Qt, like the native adapter).
    virtual bool sectionVisualX(int logicalIndex, int *viewportX) const
    {
        Q_UNUSED(logicalIndex);
        Q_UNUSED(viewportX);
        return false;
    }
    /// True while this renderer draws at least one section away from its committed
    /// position (a drag preview or a running transition). A view only mirrors the
    /// sections while this is true, so an idle header costs nothing.
    virtual bool hasVisualSectionGeometry() const { return false; }
    /// Installs the callback the renderer invokes every time it places its sections
    /// on a visual position - every animation frame, plus the frame that settles
    /// back onto the committed geometry. It runs while the layout is still being
    /// applied, so a listener that moves widgets does not flicker.
    ///
    /// Pass an empty function to detach. Renderers without visual state ignore it.
    virtual void setVisualGeometryCallback(std::function<void()> callback)
    {
        Q_UNUSED(callback);
    }

    /// The model's *items* changed identity without changing the section set: a row (or
    /// column) was moved, inserted or removed, so the label a materialized section shows
    /// belongs to a different item now. A renderer that caches what the label model said -
    /// the default label adapter does - has to re-read it here; a renderer that lays its
    /// sections out from the geometry alone ignores this.
    ///
    /// The section *count* changes go through setGeometryModel()/the geometry's own
    /// signals, so this is only about the text/state of the sections that already exist.
    virtual void refreshSectionLabels() {}
};
} // namespace viv
