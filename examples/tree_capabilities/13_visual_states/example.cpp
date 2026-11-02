#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace tree_visual_states {

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
        // Every bind replaces the previous node's content, including after scrolling.
        auto *label = static_cast<QLabel *>(widget);
        label->setText(index.data().toString());
        // Keep the widget narrower than the row so the background modes differ.
        // Recompute on every bind because recycled nodes can have different text.
        label->setMaximumWidth(qMax(1, label->sizeHint().width()));
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(480, 36);
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
        auto *description = new QLabel(QStringLiteral("13 状态背景：hover 和 selected 由框架绘制，背景范围独立于分割线，可修改颜色、圆角和曲线。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        enabled = new QCheckBox(QStringLiteral("状态背景"), this);
        enabled->setChecked(true);
        controls->addWidget(enabled);
        extent = new QComboBox(this);
        extent->addItems({QStringLiteral("仅节点 (NodeOnly)"), QStringLiteral("节点及图标 (NodeAndIcon)"),
                          QStringLiteral("整行 (FullWidth)"),
                          QStringLiteral("仅控件 (WidgetOnly)"), QStringLiteral("控件及图标 (WidgetAndIcon)")});
        controls->addWidget(extent);
        radius = new QSpinBox(this);
        radius->setRange(0, 24);
        radius->setValue(6);
        radius->setPrefix(QStringLiteral("圆角 "));
        controls->addWidget(radius);
        controls->addStretch();
        layout->addLayout(controls);
        auto *options = new QHBoxLayout;
        duration = new QSpinBox(this);
        duration->setRange(0, 800);
        duration->setValue(180);
        duration->setPrefix(QStringLiteral("时长 ms "));
        options->addWidget(duration);
        curve = new QComboBox(this);
        curve->addItems({QStringLiteral("OutCubic"), QStringLiteral("Linear")});
        options->addWidget(curve);
        colors = new QCheckBox(QStringLiteral("暖色背景"), this);
        colors->setChecked(false);
        options->addWidget(colors);
        options->addStretch();
        layout->addLayout(options);
        auto *backgroundGeometry = new QHBoxLayout;
        backgroundHeight = new QSpinBox(this);
        backgroundHeight->setRange(0, 120);
        backgroundHeight->setSpecialValueText(QStringLiteral("自动高度"));
        backgroundHeight->setPrefix(QStringLiteral("背景高度 "));
        backgroundHeight->setToolTip(QStringLiteral("0 为自动；固定高度在扣除边距后的区域内垂直居中，不超过剩余高度。"));
        backgroundGeometry->addWidget(backgroundHeight);
        marginLeft = new QSpinBox(this);
        marginLeft->setRange(0, 80);
        marginLeft->setPrefix(QStringLiteral("左边距 "));
        backgroundGeometry->addWidget(marginLeft);
        marginTop = new QSpinBox(this);
        marginTop->setRange(0, 80);
        marginTop->setPrefix(QStringLiteral("上边距 "));
        backgroundGeometry->addWidget(marginTop);
        marginRight = new QSpinBox(this);
        marginRight->setRange(0, 80);
        marginRight->setPrefix(QStringLiteral("右边距 "));
        backgroundGeometry->addWidget(marginRight);
        marginBottom = new QSpinBox(this);
        marginBottom->setRange(0, 80);
        marginBottom->setPrefix(QStringLiteral("下边距 "));
        backgroundGeometry->addWidget(marginBottom);
        backgroundGeometry->addStretch();
        layout->addLayout(backgroundGeometry);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 12; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 2; ++leaf)
                        node->appendRow(new QStandardItem(QStringLiteral("叶子 %1.%2.%3")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1)));
                }
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new TextAdapter, true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(36);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // The built-in background sits below transparent node widgets and branch decorations.
        m_view->setVisualStateBackgroundVisible(true);
        m_view->setVisualStateCornerRadius(6);
        m_view->setVisualStateAnimationDuration(180);
        m_view->setVisualStateAnimationEasingCurve(QEasingCurve::OutCubic);
        m_view->expandRecursively(m_model->index(0, 0));
        connect(enabled, &QCheckBox::toggled, m_view, &viv::VirtualTreeView::setVisualStateBackgroundVisible);
        // WidgetOnly follows the widget; WidgetAndIcon also covers a visible branch icon's cell.
        // Leaves and hidden indicators use WidgetOnly geometry, including for mouse hits.
        // NodeAndIcon similarly falls back to NodeOnly when no indicator is shown.
        connect(extent, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->setVisualStateBackgroundExtent(static_cast<viv::VirtualTreeView::VisualStateBackgroundExtent>(value));
        });
        extent->setCurrentIndex(2);
        // Insets affect state backgrounds and mouse hits, never the node widget or row height.
        // With a 36px row, top/bottom margins of 4 leave a 28px state background.
        const auto applyMargins = [this] {
            m_view->setVisualStateBackgroundMargins(
                marginLeft->value(), marginTop->value(), marginRight->value(), marginBottom->value());
        };
        for (QSpinBox *spin : {marginLeft, marginTop, marginRight, marginBottom})
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, applyMargins);
        // Zero restores automatic height. A positive height is centered within the inset area.
        connect(backgroundHeight, QOverload<int>::of(&QSpinBox::valueChanged), m_view,
                &viv::VirtualTreeView::setVisualStateBackgroundHeight);
        connect(radius, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualTreeView::setVisualStateCornerRadius);
        connect(duration, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualItemView::setVisualStateAnimationDuration);
        connect(curve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->setVisualStateAnimationEasingCurve(value == 0 ? QEasingCurve::OutCubic : QEasingCurve::Linear);
        });
        connect(colors, &QCheckBox::toggled, this, [this](bool enabled) {
            m_view->setHoverBackgroundColor(QColor(enabled ? QStringLiteral("#fff0cf") : QStringLiteral("#e7f4ed")));
            m_view->setSelectedBackgroundColor(QColor(enabled ? QStringLiteral("#f2c884") : QStringLiteral("#b9d9f1")));
        });
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QCheckBox *enabled = nullptr;
    QComboBox *extent = nullptr;
    QSpinBox *radius = nullptr;
    QSpinBox *duration = nullptr;
    QSpinBox *backgroundHeight = nullptr;
    QSpinBox *marginLeft = nullptr;
    QSpinBox *marginTop = nullptr;
    QSpinBox *marginRight = nullptr;
    QSpinBox *marginBottom = nullptr;
    QComboBox *curve = nullptr;
    QCheckBox *colors = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_visual_states
