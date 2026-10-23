#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/headerwidgetadapter.h>
#include <virtualitemviews/virtualitemview.h>

#include <QWidget>

class QPainter;

namespace viv {

class VirtualHeaderView;
class VirtualTableView;

/// Passive, reusable header section with binding identity and a layered paint lifecycle.
/// No panel, state background, separators or text are drawn by this base class.
/// VirtualHeaderView owns pointer gestures; the adapter updates the logical index on bind.
class VIRTUALITEMVIEWS_EXPORT HeaderSection : public QWidget
{
    Q_OBJECT

public:
    /// Creates a section without taking focus from the owning header's gestures.
    explicit HeaderSection(QWidget *parent = nullptr);

    /// Current logical section, or -1 while unbound. Read it when handling an event.
    int logicalIndex() const { return m_logicalIndex; }
    /// Replaces binding identity; adapters must clear it when recycling the widget.
    void setLogicalIndex(int logicalIndex);
    /// Immediate header owner, or nullptr for a standalone section or corner widget.
    const VirtualHeaderView *headerView() const;
    /// Owning table, or nullptr for a header used outside a table.
    const VirtualTableView *tableView() const;
    /// Header axis; standalone sections and corner widgets use Qt::Horizontal.
    Qt::Orientation orientation() const;
    /// Current header state and shared animation progress, independent of background visibility.
    /// Standalone and unbound sections return an empty state.
    VirtualItemView::VisualState visualState() const;

protected:
    /// Calls background, content and foreground in order, isolating their painter state.
    void paintEvent(QPaintEvent *event) override;
    /// First layer, empty by default. Coordinates are section-local.
    virtual void paintBackground(QPainter *painter);
    /// Middle layer, empty by default. Override for text, icons or other custom content.
    virtual void paintContent(QPainter *painter);
    /// Last layer, empty by default. Override for borders or overlays.
    virtual void paintForeground(QPainter *painter);
    /// Repaints when style, palette, font or enabled state changes.
    void changeEvent(QEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    /// Repaints on pointer entry without consuming the header's gestures.
    void enterEvent(QEnterEvent *event) override;
#else
    /// Repaints on pointer entry without consuming the header's gestures.
    void enterEvent(QEvent *event) override;
#endif
    /// Repaints on pointer exit without consuming the header's gestures.
    void leaveEvent(QEvent *event) override;

private:
    int m_logicalIndex = -1;
};

/// Adapter for HeaderSection and its subclasses; binds identity without providing labels.
/// Override createSection() for styled or custom content while retaining bind/unbind.
class VIRTUALITEMVIEWS_EXPORT HeaderSectionAdapter : public HeaderWidgetAdapter
{
public:
    /// Creates an unstyled HeaderSection by default.
    QWidget *createSection(WidgetType type, QWidget *parent) override;
    /// Supplies the current logical identity on every bind, including reuse.
    void bindSection(QWidget *widget, int logicalIndex) override;
    /// Clears the logical identity before the section enters the pool.
    void unbindSection(QWidget *widget, int logicalIndex) override;
};

} // namespace viv
