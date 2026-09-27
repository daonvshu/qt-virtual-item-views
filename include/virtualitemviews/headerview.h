#pragma once

#include <virtualitemviews/global.h>

#include <QPoint>
#include <QVector>
#include <QWidget>

#include <QAbstractItemModel>
#include <QColor>
#include <functional>

namespace viv {

class HeaderGeometry;

/// Colour the current style paints a header section separator with (probed by rendering a
/// small section and reading its edge pixel). The pane boundary lines and the built-in
/// section widgets use it, so every line that separates two sections - in either axis and
/// in the body - is the style's line instead of a guessed palette role.
///
/// \a context supplies the style and the palette (pass the view or a widget in it).
VIRTUALITEMVIEWS_EXPORT QColor headerSectionSeparatorColor(const QWidget *context);

/// Renderer abstraction of a table header (architecture document §15/§16/§17).
///
/// The implementation is `LabelHeaderView` (the default: a `VirtualHeaderView` plus the
/// built-in label adapter) or any renderer the application installs - `VirtualHeaderView`
/// with its own section widgets, or a custom implementation of this interface.
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
    virtual Qt::Orientation orientation() const = 0;
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
    /// committed position instead of teleporting; the table body keeps reading the
    /// committed HeaderGeometry, so it never follows an intermediate frame. A
    /// renderer that cannot do this - the native QHeaderView adapter, whose
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
