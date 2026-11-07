#pragma once

#include <virtualitemviews/global.h>
#include <QScrollArea>

namespace viv {
class WrapLayout;

/// A scrolling host for a small collection of real widgets. Auto height is on
/// by default and fixes the host height to its wrapped content, independently
/// of the parent layout's available height. An optional auto-height maximum
/// bounds the host and lets excess content scroll internally.
/// The content widget and layout belong to this host and must not be replaced.
class VIRTUALITEMVIEWS_EXPORT WrapWidget : public QScrollArea
{
    Q_OBJECT
public:
    explicit WrapWidget(QWidget *parent = nullptr);
    ~WrapWidget() override;
    WrapLayout *wrapLayout() const { return m_layout; }
    /// Transfers widget ownership to the content host.
    void addWidget(QWidget *widget);
    void insertWidget(int index, QWidget *widget);
    /// Removes, hides and detaches the widget; the caller takes ownership.
    QWidget *takeWidget(int index);
    /// Deletes all contained widgets and layout items.
    void clear();
    /// Fixes the host to its content height; disabling restores the previous
    /// QWidget height constraints and vertical size policy.
    void setAutoHeightEnabled(bool enabled);
    bool autoHeightEnabled() const { return m_autoHeight; }
    /// Limits the total auto height, including decorations. Zero (the default)
    /// removes the limit; negative values are treated as zero. Use this instead
    /// of QWidget height setters while auto height owns the host height.
    void setAutoHeightMaximumHeight(int height);
    int autoHeightMaximumHeight() const { return m_autoHeightMaximumHeight; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;

private:
    void scheduleUpdate();
    void updateContent();
    int autoHeightDecorationExtent() const;
    using QScrollArea::setWidget;
    using QScrollArea::setWidgetResizable;
    QWidget *m_content = nullptr;
    WrapLayout *m_layout = nullptr;
    bool m_autoHeight = true;
    bool m_updatePending = false;
    bool m_updating = false;
    bool m_destroying = false;
    bool m_fixedHeightApplied = false;
    int m_autoHeightMaximumHeight = 0;
    int m_previousMinimumHeight = 0;
    int m_previousMaximumHeight = QWIDGETSIZE_MAX;
    QSize m_lastHint;
    QSizePolicy::Policy m_previousPolicy = QSizePolicy::Expanding;
};

} // namespace viv
