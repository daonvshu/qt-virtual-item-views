#pragma once

#include <virtualitemviews/global.h>
#include <virtualitemviews/headerwidgetadapter.h>
#include <virtualitemviews/virtualheaderview.h>

#include <QPointer>
#include <QString>
#include <QWidget>

class QAbstractItemModel;

namespace viv {

class HeaderGeometry;

/// Section widget of the built-in label header: one label, painted the way the
/// current style paints a `QHeaderView` section (panel, label, sort indicator), so
/// a widget based header looks like the native one out of the box.
///
/// The widget is passive on purpose: a header section must not consume mouse
/// events, because `VirtualHeaderView` derives "resize the column" / "drag the
/// column" from the pointer position it sees through its own filters on the
/// section widgets (§25). It only repaints itself on hover / style / palette /
/// font changes.
class VIRTUALITEMVIEWS_EXPORT LabelHeaderSection : public QWidget
{
    Q_OBJECT

public:
    explicit LabelHeaderSection(QWidget *parent = nullptr);

    QString text() const { return m_text; }
    /// Sort indicator: -1 draws none, otherwise a Qt::SortOrder.
    int sortOrder() const { return m_sortOrder; }

    void setText(const QString &text);
    void setSortOrder(int order);

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) override;
#else
    void enterEvent(QEvent *event) override;
#endif
    void leaveEvent(QEvent *event) override;

private:
    QString m_text;
    int m_sortOrder = -1;
};

/// The header adapter of the built-in label header: one label per section, taken
/// from the label model's `headerData()` (§19).
///
/// It is a normal HeaderWidgetAdapter, so it works for a hand-built
/// VirtualHeaderView too, and it is meant to be extended: override labelText() or
/// sortOrderFor() to change what a section shows without writing a section widget.
class VIRTUALITEMVIEWS_EXPORT LabelHeaderAdapter : public HeaderWidgetAdapter
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

/// The header a table installs when the application does not install one: a
/// VirtualHeaderView that already carries the built-in LabelHeaderAdapter.
///
/// It is a widget header, so everything that needs real section widgets works on
/// it too: hit testing and the resize cursor (§25), drag reordering with the "make
/// room" tween (§22), the visual transition of a section move and the body
/// following it (§23/§24), and the frozen / extra panes, which the table renders
/// with clones of the same kind (§43). Use `setHorizontalHeader(nullptr)` to get
/// back to it, `setHorizontalHeaderVisible(false)` to hide the header completely,
/// and `setVerticalHeader(new LabelHeaderView(Qt::Vertical))` for a different kind of
/// strip (the row-number strip uses the same renderer on the other axis).
class VIRTUALITEMVIEWS_EXPORT LabelHeaderView : public VirtualHeaderView
{
    Q_OBJECT

public:
    explicit LabelHeaderView(Qt::Orientation orientation = Qt::Horizontal,
                            QWidget *parent = nullptr);
    ~LabelHeaderView() override;

    /// The adapter the sections are built with. Replacing it (setAdapter()) is
    /// allowed; a subclassed LabelHeaderAdapter is the cheap way to change what a
    /// label shows without building section widgets.
    LabelHeaderAdapter *labelAdapter() const
    {
        return dynamic_cast<LabelHeaderAdapter *>(adapter());
    }
};

} // namespace viv
