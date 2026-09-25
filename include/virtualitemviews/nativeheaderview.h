#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/tablepane.h>

#include <QHeaderView>

class QAbstractItemModel;

namespace viv {

class HeaderGeometry;

/// Renderer abstraction of a table header (architecture document §15/§16/§17).
///
/// Two implementations are foreseen:
///  - NativeHeaderView (this file): a QHeaderView adapter driven by
///    HeaderGeometry, and
///  - VirtualHeaderView (v0.5): QWidget based sections with
///    HeaderWidgetAdapter + recycler.
///
/// Both consume the same HeaderGeometry, so the table body never depends on how
/// the header is rendered. The interface is intentionally not a QWidget: the
/// native implementation is already a QHeaderView (a QWidget).
class VIRTUALITEMVIEWS_EXPORT HeaderViewInterface
{
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
};

/// QHeaderView driven by HeaderGeometry.
///
/// The geometry is the single source of truth: user gestures (resize, move,
/// sort click) are written into it and every geometry change is reflected back
/// into the header. Syncing compares before writing, so it neither invalidates
/// QHeaderView's caches needlessly nor recurses.
class VIRTUALITEMVIEWS_EXPORT NativeHeaderView : public QHeaderView, public HeaderViewInterface
{
    Q_OBJECT

public:
    explicit NativeHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);
    ~NativeHeaderView() override;

    void setGeometryModel(HeaderGeometry *geometry) override;
    HeaderGeometry *geometryModel() const override { return m_geometry; }
    QWidget *headerWidget() override { return this; }
    Qt::Orientation orientation() const override { return QHeaderView::orientation(); }
    void setLabelModel(QAbstractItemModel *model) override;
    void setSortInteractionEnabled(bool enabled) override;

    /// Horizontal offset of the header's viewport; the table sets this so that
    /// header and body never drift apart.
    void setViewportOffset(int offset);

    /// Restricts the header to one pane (§31): only \a logicalColumns are shown
    /// (sizes, visibility and order still come from HeaderGeometry, a filter is
    /// never a width copy). When \a frozen the header ignores the geometry's
    /// offset, because a frozen pane never scrolls; user section moves are
    /// disabled while a filter is active.
    void setPaneFilter(const QVector<int> &logicalColumns, bool frozen) override;
    void clearPaneFilter() override;
    bool hasPaneFilter() const { return m_paneFilterActive; }

    /// Offset of the pane's own content (§43 "advanced panes"); see
    /// HeaderViewInterface::setPaneOffset(). A frozen pane keeps offset 0 even
    /// when the geometry scrolls.
    ///
    /// An explicit offset wins over the geometry's viewport offset with or without a
    /// pane filter, so a renderer that is placed on one band of a split header (the
    /// vertical strip of a frozen row pane, for example) keeps its own offset even when
    /// the shared geometry scrolls or its sections change size.
    void setPaneOffset(qint64 offset) override;

    /// Colour the current style paints a header section separator with (probed by
    /// rendering a section and reading its edge pixel). The pane boundary line -
    /// header edge and the body line - uses it, so it matches the separators
    /// between the other columns instead of a guessed palette role.
    static QColor sectionSeparatorColor(const QWidget *context);

    /// Paints a pane boundary line into \a rect (its layout is defined by the
    /// style; solid lines fill the rect, dashed ones are centred on it).
    static void drawPaneSeparator(QPainter *painter, const QRect &rect,
                                  const PaneSeparatorStyle &style,
                                  const QColor &styleSeparatorColor);

    /// True while the geometry is being applied to this header (the resulting
    /// QHeaderView signals must not be written back into the geometry).
    bool isApplyingGeometry() const { return m_applyingToHeader; }

private:
    void connectGeometry(HeaderGeometry *geometry, bool connectSignals);
    void syncHeaderFromGeometry();
    /// Applies the geometry's visual order to this header (a section move that
    /// did not come from a user drag on this header would otherwise leave the
    /// header and the body out of sync).
    void applyVisualOrder();
    void syncGeometryFromHeaderSectionSize(int logicalIndex, int size);
    void syncGeometryFromHeaderMove(int logicalIndex, int oldVisualIndex, int newVisualIndex);
    void onHeaderSectionResized(int logicalIndex, int oldSize, int newSize);
    void onHeaderSectionMoved(int logicalIndex, int oldVisualIndex, int newVisualIndex);
    void onGeometryChanged();
    void applySection(int logicalIndex);
    /// Offset this header lays its sections out with: its own pane offset, or the
    /// geometry's viewport offset when it follows the committed geometry.
    qint64 effectivePaneOffset() const;

    HeaderGeometry *m_geometry = nullptr;
    /// True while the geometry is being applied to the header, so that the
    /// resulting QHeaderView signals do not write back into the geometry.
    bool m_applyingToHeader = false;
    bool m_sortInteractionEnabled = false;
    /// Pane filter (§31): the logical sections this header shows.
    QVector<int> m_paneFilter;
    bool m_paneFilterActive = false;
    bool m_frozenPane = false;
    qint64 m_paneOffset = kFollowGeometryOffset;
    /// Set once when the geometry's extent does not fit into QHeaderView's int
    /// range (diagnostics; see syncHeaderFromGeometry()).
    bool m_extentOverflowWarned = false;
};

} // namespace viv
