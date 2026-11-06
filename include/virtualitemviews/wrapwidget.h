#pragma once

#include <virtualitemviews/global.h>
#include <QScrollArea>

namespace viv {
class WrapLayout;

/// A scrolling host for a small collection of real widgets. Auto height is on
/// by default, bounded by maximumHeight and the space granted by the parent.
/// The content widget and layout belong to this host and must not be replaced.
class VIRTUALITEMVIEWS_EXPORT WrapWidget : public QScrollArea
{
    Q_OBJECT
public:
    explicit WrapWidget(QWidget *parent = nullptr);
    WrapLayout *wrapLayout() const { return m_layout; }
    /// Transfers widget ownership to the content host.
    void addWidget(QWidget *widget);
    void insertWidget(int index, QWidget *widget);
    /// Removes, hides and detaches the widget; the caller takes ownership.
    QWidget *takeWidget(int index);
    /// Deletes all contained widgets and layout items.
    void clear();
    void setAutoHeightEnabled(bool enabled);
    bool autoHeightEnabled() const { return m_autoHeight; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;

private:
    void scheduleUpdate();
    void updateContent();
    using QScrollArea::setWidget;
    using QScrollArea::setWidgetResizable;
    QWidget *m_content = nullptr;
    WrapLayout *m_layout = nullptr;
    bool m_autoHeight = true;
    bool m_updatePending = false;
    bool m_updating = false;
    QSize m_lastHint;
    QSizePolicy::Policy m_previousPolicy = QSizePolicy::Expanding;
};

} // namespace viv
