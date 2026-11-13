#include <QComboBox>
#include "ui_row_spacing.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QStringListModel>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListRowSpacingPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListRowSpacingPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Content references keep offscreen endpoints uncut until they scroll into view.
        connect(ui->lineReference, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setGridLineMarginReference(mode == 0
                ? viv::VirtualItemView::GridLineMarginReference::Viewport
                : viv::VirtualItemView::GridLineMarginReference::Content);
        });
        const auto updateLineMargins = [this] {
            // These are overall endpoints, not padding repeated on each cell.
            ui->view->setGridLineMargins(QMargins(ui->lineLeft->value(), 0,
                ui->lineRight->value(), 0));
        };
        connect(ui->lineLeft, QOverload<int>::of(&QSpinBox::valueChanged), this, updateLineMargins);
        connect(ui->lineRight, QOverload<int>::of(&QSpinBox::valueChanged), this, updateLineMargins);
        const auto updateStateMargins = [this] {
            // Paint-only insets preserve existing widget geometry and mouse hits.
            ui->view->setRowVisualStateMargins(ui->stateLeft->value(), ui->stateRight->value());
        };
        connect(ui->stateLeft, QOverload<int>::of(&QSpinBox::valueChanged), this, updateStateMargins);
        connect(ui->stateRight, QOverload<int>::of(&QSpinBox::valueChanged), this, updateStateMargins);

        QStringList records;
        for (int row = 0; row < 100; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        auto *model = new QStringListModel(records, ui->view);
        ui->view->setAdapter(new TextAdapter, true);
        ui->view->setUniformItemHeight(36);
        // Spacing is outside the row widget; the last row has no trailing gap.
        ui->view->setRowSpacing(24);
        ui->view->setModel(model);
        // These direct signal-to-slot connections keep view configuration live.
        connect(ui->gap, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualListView::setRowSpacing);
        // Grid-line width uses physical pixels; row height and spacing use logical pixels.
        connect(ui->width, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualListView::setRowGridLineWidth);
        // The switch controls both zero-spacing row lines and gap boundary lines.
        connect(ui->lines, &QCheckBox::toggled, ui->view, &viv::VirtualListView::setRowGridLinesVisible);
        connect(ui->color, &QCheckBox::toggled, this, [this](bool blue) {
            // An invalid QColor restores the palette's default separator color.
            ui->view->setRowGridLineColor(blue ? QColor(QStringLiteral("#3074b5")) : QColor());
        });
        connect(ui->content, &QCheckBox::toggled, this, [this](bool enabled) {
            if (!enabled) {
                // Clear the factory to return to plain spacing without content widgets.
                ui->view->setRowSpacingFactory({});
                return;
            }
            // The factory creates a reusable gap widget; the binder refreshes it
            // whenever that widget is assigned to a different gap.
            ui->view->setRowSpacingFactory(
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

};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_row_spacing
