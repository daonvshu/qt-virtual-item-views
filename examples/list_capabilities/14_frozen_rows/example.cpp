#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStringListModel>
#include <QVBoxLayout>

namespace list_frozen_rows {

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(400, 36);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(QStringLiteral("14 冻结行：顶部和底部固定显示，中间区域独立滚动。"), this));
        auto *controls = new QHBoxLayout;
        m_top = new QSpinBox(this);
        m_bottom = new QSpinBox(this);
        m_width = new QSpinBox(this);
        m_top->setRange(0, 12);
        m_bottom->setRange(0, 12);
        m_width->setRange(0, 6);
        m_top->setValue(2);
        m_bottom->setValue(1);
        m_width->setValue(2);
        m_top->setPrefix(QStringLiteral("顶部行 "));
        m_bottom->setPrefix(QStringLiteral("底部行 "));
        m_width->setPrefix(QStringLiteral("分隔线宽 "));
        for (auto *spin : {m_top, m_bottom, m_width})
            controls->addWidget(spin);
        layout->addLayout(controls);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 100; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        // The framework paints rounded hover/selection backgrounds in every pane,
        // including frozen rows; the adapter only binds transparent label text.
        m_view->setVisualStateBackgroundVisible(true);
        m_view->setVisualStateCornerRadius(8);
        m_view->setAdapter(new TextAdapter, true);
        m_view->setUniformItemHeight(36);
        m_view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        m_view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        m_view->setModel(new QStringListModel(records, m_view));
        // Freeze a prefix at the top and a suffix at the bottom; the remaining
        // rows scroll between them. Freezing does not duplicate model data.
        // The effective counts are limited by row count and available viewport height.
        m_view->setFrozenRows(2);
        m_view->setFrozenBottomRows(1);
        connect(m_top, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualItemView::setFrozenRows);
        connect(m_bottom, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualItemView::setFrozenBottomRows);
        connect(m_width, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applySeparator(); });
        applySeparator();
    }

    void applySeparator()
    {
        viv::PaneSeparatorStyle style;
        style.width = m_width->value();
        style.color = QColor(QStringLiteral("#3074b5"));
        style.lineStyle = Qt::DashLine;
        // Style only the boundary between row panes, not each row's grid line.
        // A zero separator width hides these pane boundaries.
        m_view->setItemPaneSeparatorStyle(style);
    }

    viv::VirtualListView *m_view = nullptr;
    QSpinBox *m_top = nullptr;
    QSpinBox *m_bottom = nullptr;
    QSpinBox *m_width = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_frozen_rows
