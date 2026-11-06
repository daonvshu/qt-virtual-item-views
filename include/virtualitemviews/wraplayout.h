#pragma once

#include <virtualitemviews/global.h>
#include <QLayout>
#include <QVector>

namespace viv {

/// Small collections of real widgets, packed in order at a uniform height.
/// Widths use each item's size hint and minimum/maximum constraints. Hidden
/// widgets do not occupy space (unless retainSizeWhenHidden is enabled).
/// This layout supplies heightForWidth; use WrapWidget for bounded scrolling.
class VIRTUALITEMVIEWS_EXPORT WrapLayout : public QLayout
{
    Q_OBJECT
public:
    explicit WrapLayout(QWidget *parent = nullptr);
    ~WrapLayout() override;
    void addItem(QLayoutItem *item) override;
    /// Inserts a widget; the parent widget takes QObject ownership.
    void insertWidget(int index, QWidget *widget);
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    /// Removes a layout item without deleting it or its widget.
    QLayoutItem *takeAt(int index) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    void setGeometry(const QRect &rect) override;
    void invalidate() override;
    void setFixedItemHeight(int height);
    int fixedItemHeight() const { return m_height; }
    void setItemSpacing(int pixels);
    int itemSpacing() const { return m_itemSpacing; }
    void setRowSpacing(int pixels);
    int rowSpacing() const { return m_rowSpacing; }
    /// Logical leading/center/trailing alignment; AlignAbsolute keeps physical sides.
    void setLineAlignment(Qt::Alignment alignment);
    Qt::Alignment lineAlignment() const { return m_alignment; }

signals:
    /// Invalidated by child visibility, size hints or layout configuration.
    void layoutChanged();

private:
    int arrange(const QRect &rect, bool apply) const;
    QVector<QLayoutItem *> m_items;
    int m_height = 32;
    int m_itemSpacing = 8;
    int m_rowSpacing = 8;
    Qt::Alignment m_alignment = Qt::AlignLeft;
};

} // namespace viv
