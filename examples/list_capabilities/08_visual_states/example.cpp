#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStringListModel>
#include <QVBoxLayout>

namespace list_visual_states {

class StateAdapter : public viv::WidgetAdapter
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
        auto *row = static_cast<QLabel *>(widget);
        row->clear();
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
        auto *intro = new QLabel(QStringLiteral("08 状态背景：移动鼠标、点击选择；框架默认绘制 item 背景，可直接设置圆角、颜色和动画。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *controls = new QHBoxLayout;
        m_duration = new QSpinBox(this);
        m_duration->setRange(0, 1000);
        m_duration->setValue(180);
        m_duration->setSuffix(QStringLiteral(" ms"));
        m_colors = new QComboBox(this);
        m_colors->addItems({QStringLiteral("蓝色"), QStringLiteral("绿色")});
        m_easing = new QComboBox(this);
        m_easing->addItem(QStringLiteral("OutCubic"), int(QEasingCurve::OutCubic));
        m_easing->addItem(QStringLiteral("InOutCubic"), int(QEasingCurve::InOutCubic));
        m_easing->addItem(QStringLiteral("Linear"), int(QEasingCurve::Linear));
        m_radius = new QSpinBox(this);
        m_radius->setRange(0, 24);
        m_radius->setValue(8);
        m_radius->setPrefix(QStringLiteral("圆角 "));
        m_radius->setSuffix(QStringLiteral(" px"));
        controls->addWidget(m_duration);
        controls->addWidget(m_colors);
        controls->addWidget(m_easing);
        controls->addWidget(m_radius);
        layout->addLayout(controls);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 100; ++row)
            records.append(QStringLiteral("条目 %1 · 支持 hover 与 selected 同时存在").arg(row + 1));
        auto *model = new QStringListModel(records, m_view);
        // The framework paints states by default; the adapter only provides text.
        m_view->setAdapter(new StateAdapter, true);
        m_view->setUniformItemHeight(36);
        // Radius only affects the item; row spacing always keeps its own background.
        m_view->setVisualStateCornerRadius(8);
        // Spacing stays neutral while hover and selection animate inside each item.
        m_view->setRowSpacing(8);
        m_view->setRowGridLinesVisible(false);
        m_view->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        // The view paints these colors below the transparent row labels.
        m_view->setHoverBackgroundColor(QColor(QStringLiteral("#e6effb")));
        m_view->setSelectedBackgroundColor(QColor(QStringLiteral("#afcbee")));
        // OutCubic responds quickly when the pointer only visits a row briefly.
        // OutCubic is also the framework default; the explicit call demonstrates the API.
        m_view->setVisualStateAnimationEasingCurve(QEasingCurve::OutCubic);
        m_view->setModel(model);
        // Duration is shared by hover and selection transitions; 0 switches immediately.
        connect(m_duration, QOverload<int>::of(&QSpinBox::valueChanged),
                m_view, &viv::VirtualItemView::setVisualStateAnimationDuration);
        // Zero gives square corners; the radius applies to both visual states.
        connect(m_radius, QOverload<int>::of(&QSpinBox::valueChanged),
                m_view, &viv::VirtualListView::setVisualStateCornerRadius);
        connect(m_easing, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            // The same curve controls hover and selection, including fade-out.
            const auto type = static_cast<QEasingCurve::Type>(m_easing->currentData().toInt());
            m_view->setVisualStateAnimationEasingCurve(QEasingCurve(type));
        });
        connect(m_colors, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int color) {
            // Changing a visual-state color notifies materialized rows to repaint.
            m_view->setHoverBackgroundColor(QColor(color == 0 ? QStringLiteral("#e6effb") : QStringLiteral("#e1f1e5")));
            m_view->setSelectedBackgroundColor(QColor(color == 0 ? QStringLiteral("#afcbee") : QStringLiteral("#a2d4b0")));
        });
    }

    viv::VirtualListView *m_view = nullptr;
    QSpinBox *m_duration = nullptr;
    QSpinBox *m_radius = nullptr;
    QComboBox *m_colors = nullptr;
    QComboBox *m_easing = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_visual_states
