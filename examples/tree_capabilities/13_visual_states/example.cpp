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
        static_cast<QLabel *>(widget)->setText(index.data().toString());
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
        extent->addItems({QStringLiteral("仅节点"), QStringLiteral("节点及图标"), QStringLiteral("整行")});
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
        m_view->setRowSpacing(6);
        m_view->expandRecursively(m_model->index(0, 0));
        connect(enabled, &QCheckBox::toggled, m_view, &viv::VirtualTreeView::setVisualStateBackgroundVisible);
        connect(extent, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            m_view->setVisualStateBackgroundExtent(static_cast<viv::VirtualTreeView::VisualStateBackgroundExtent>(value));
        });
        extent->setCurrentIndex(2);
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
    QComboBox *curve = nullptr;
    QCheckBox *colors = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_visual_states
