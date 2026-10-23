#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/headerwidgetadapter.h>
#include <virtualitemviews/styledheadersection.h>
#include <virtualitemviews/virtualheaderview.h>

#include <QPointer>
#include <QString>
#include <QWidget>

class QAbstractItemModel;

namespace viv {

class HeaderGeometry;

/// Text and sort-indicator content for the built-in label header.
/// StyledHeaderSection supplies its panel, state backgrounds, corners and separators;
/// HeaderSection supplies binding identity, owner/state queries and passive input behavior.
class VIRTUALITEMVIEWS_EXPORT LabelHeaderSection : public StyledHeaderSection
{
    Q_OBJECT

public:
    explicit LabelHeaderSection(QWidget *parent = nullptr);

    QString text() const { return m_text; }
    /// Sort indicator: -1 draws none, otherwise a Qt::SortOrder.
    int sortOrder() const { return m_sortOrder; }

    void setText(const QString &text);
    void setSortOrder(int order);
    /// Model-specific alignment; empty falls back to the owning header's default.
    void setTextAlignment(Qt::Alignment alignment);
    /// Model-specific foreground; NoBrush falls back to the owning header's default.
    void setForegroundBrush(const QBrush &brush);

protected:
    /// Adds label and sort-indicator data to the inherited panel option.
    void initStyleOption(QStyleOptionHeader *option) const override;
    /// Prepares content geometry with header padding; never changes the panel rectangle.
    void initContentStyleOption(QStyleOptionHeader *option) const;
    /// Draws only the text and sort indicator, between background and separators.
    void paintContent(QPainter *painter) override;

private:
    QString m_text;
    int m_sortOrder = -1;
    Qt::Alignment m_textAlignment;
    QBrush m_foregroundBrush;
};

/// The header adapter of the built-in label header: one label per section, taken
/// from the label model's `headerData()` (§19).
///
/// VirtualHeaderView installs and owns this adapter by default. It can be extended:
/// override labelText() or
/// sortOrderFor() to change what a section shows without writing a section widget.
class VIRTUALITEMVIEWS_EXPORT LabelHeaderAdapter : public HeaderSectionAdapter
{
public:
    QWidget *createSection(WidgetType type, QWidget *parent) override;
    void setLabelModel(QAbstractItemModel *model) override;
    void setGeometryModel(HeaderGeometry *geometry) override;
    void bindSection(QWidget *widget, int logicalIndex) override;
    void unbindSection(QWidget *widget, int logicalIndex) override;

protected:
    /// Text of \a logicalIndex on \a orientation's axis (the label model's
    /// headerData, empty without one).
    virtual QString labelText(int logicalIndex, Qt::Orientation orientation) const;
    /// Sort indicator of \a logicalIndex: -1 when the geometry marks no section or
    /// another one.
    virtual int sortOrderFor(int logicalIndex) const;

    QAbstractItemModel *labelModel() const { return m_model.data(); }
    HeaderGeometry *geometry() const { return m_geometry.data(); }

private:
    /// Both are collaborators of the header, not of this adapter: watched, so a
    /// model or geometry the application drops cannot be dereferenced here.
    QPointer<QAbstractItemModel> m_model;
    QPointer<HeaderGeometry> m_geometry;
};

} // namespace viv
