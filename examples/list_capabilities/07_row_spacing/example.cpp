#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStringListModel>
#include <QVBoxLayout>

namespace list_row_spacing {

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
        layout->addWidget(new QLabel(QStringLiteral("07 行间距与分割线：间距不属于行控件高度，末行后没有间距。"), this));
        auto *controls = new QHBoxLayout;
        m_gap = new QSpinBox(this);
        m_gap->setRange(0, 60);
        m_gap->setValue(24);
        m_gap->setPrefix(QStringLiteral("间距 "));
        m_width = new QSpinBox(this);
        m_width->setRange(1, 6);
        m_width->setValue(1);
        m_width->setPrefix(QStringLiteral("线宽 "));
        m_lines = new QCheckBox(QStringLiteral("分割线"), this);
        m_lines->setChecked(true);
        m_color = new QCheckBox(QStringLiteral("蓝色线条"), this);
        m_content = new QCheckBox(QStringLiteral("间距内放控件"), this);
        for (auto *spin : {m_gap, m_width})
            controls->addWidget(spin);
        for (auto *check : {m_lines, m_color, m_content})
            controls->addWidget(check);
        layout->addLayout(controls);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 100; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        auto *model = new QStringListModel(records, m_view);
        m_view->setAdapter(new TextAdapter, true);
        m_view->setUniformItemHeight(36);
        // Spacing is outside the row widget; the last row has no trailing gap.
        m_view->setRowSpacing(24);
        m_view->setModel(model);
        // These direct signal-to-slot connections keep view configuration live.
        connect(m_gap, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualListView::setRowSpacing);
        // Grid-line width uses physical pixels; row height and spacing use logical pixels.
        connect(m_width, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualListView::setRowGridLineWidth);
        // The switch controls both zero-spacing row lines and gap boundary lines.
        connect(m_lines, &QCheckBox::toggled, m_view, &viv::VirtualListView::setRowGridLinesVisible);
        connect(m_color, &QCheckBox::toggled, this, [this](bool blue) {
            // An invalid QColor restores the palette's default separator color.
            m_view->setRowGridLineColor(blue ? QColor(QStringLiteral("#3074b5")) : QColor());
        });
        connect(m_content, &QCheckBox::toggled, this, [this](bool enabled) {
            if (!enabled) {
                // Clear the factory to return to plain spacing without content widgets.
                m_view->setRowSpacingFactory({});
                return;
            }
            // The factory creates a reusable gap widget; the binder refreshes it
            // whenever that widget is assigned to a different gap.
            m_view->setRowSpacingFactory(
                [](const QModelIndex &, QWidget *parent) -> QWidget * {
                    auto *label = new QLabel(parent);
                    label->setAlignment(Qt::AlignCenter);
                    label->setAttribute(Qt::WA_TransparentForMouseEvents);
                    return label;
                },
                [](QWidget *widget, const QModelIndex &above) {
                    // A gap belongs to the row above it, not the row below it.
                    static_cast<QLabel *>(widget)->setText(QStringLiteral("↑ %1 下方的间距").arg(above.data().toString()));
                });
        });
    }

    viv::VirtualListView *m_view = nullptr;
    QSpinBox *m_gap = nullptr;
    QSpinBox *m_width = nullptr;
    QCheckBox *m_lines = nullptr;
    QCheckBox *m_color = nullptr;
    QCheckBox *m_content = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_row_spacing
