#include "ui_frozen_rows.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QSpinBox>
#include <QStringListModel>

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListFrozenRowsPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListFrozenRowsPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 100; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        // The framework paints rounded hover/selection backgrounds in every pane,
        // including frozen rows; the adapter only binds transparent label text.
        ui->view->setVisualStateBackgroundVisible(true);
        ui->view->setVisualStateCornerRadius(8);
        ui->view->setAdapter(new TextAdapter, true);
        ui->view->setUniformItemHeight(36);
        ui->view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        ui->view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        ui->view->setModel(new QStringListModel(records, ui->view));
        // Freeze a prefix at the top and a suffix at the bottom; the remaining
        // rows scroll between them. Freezing does not duplicate model data.
        // The effective counts are limited by row count and available viewport height.
        ui->view->setFrozenRows(2);
        ui->view->setFrozenBottomRows(1);
        connect(ui->top, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setFrozenRows);
        connect(ui->bottom, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setFrozenBottomRows);
        connect(ui->width, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applySeparator(); });
        applySeparator();
    }

    void applySeparator()
    {
        viv::PaneSeparatorStyle style;
        style.width = ui->width->value();
        style.color = QColor(QStringLiteral("#3074b5"));
        style.lineStyle = Qt::DashLine;
        // Style only the boundary between row panes, not each row's grid line.
        // A zero separator width hides these pane boundaries.
        ui->view->setItemPaneSeparatorStyle(style);
    }

};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_frozen_rows
