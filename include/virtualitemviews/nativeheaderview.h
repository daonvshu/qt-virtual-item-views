#pragma once

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
class HeaderViewInterface
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
};

/// QHeaderView driven by HeaderGeometry.
///
/// The geometry is the single source of truth: user gestures (resize, move,
/// sort click) are written into it and every geometry change is reflected back
/// into the header. Syncing compares before writing, so it neither invalidates
/// QHeaderView's caches needlessly nor recurses.
class NativeHeaderView : public QHeaderView, public HeaderViewInterface
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

    /// True while the geometry is being applied to this header (the resulting
    /// QHeaderView signals must not be written back into the geometry).
    bool isApplyingGeometry() const { return m_applyingToHeader; }

private:
    void connectGeometry(HeaderGeometry *geometry, bool connectSignals);
    void syncHeaderFromGeometry();
    void syncGeometryFromHeaderSectionSize(int logicalIndex, int size);
    void syncGeometryFromHeaderMove(int logicalIndex, int oldVisualIndex, int newVisualIndex);
    void onHeaderSectionResized(int logicalIndex, int oldSize, int newSize);
    void onHeaderSectionMoved(int logicalIndex, int oldVisualIndex, int newVisualIndex);
    void onGeometryChanged();
    void applySection(int logicalIndex);

    HeaderGeometry *m_geometry = nullptr;
    /// True while the geometry is being applied to the header, so that the
    /// resulting QHeaderView signals do not write back into the geometry.
    bool m_applyingToHeader = false;
    bool m_sortInteractionEnabled = false;
};

} // namespace viv
