#include "example.h"
#include "ui_auto_height.h"
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QPushButton>
#include <QFontMetrics>
#include <QPointer>
#include <memory>

namespace wrap_auto_height {

class Tag : public QLabel
{
public:
    explicit Tag(QWidget *parent) : QLabel(parent) { setMargin(10); }
    void setTitle(const QString &text) { m_title = text; updateText(); }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }
private:
    void updateText() { setText(fontMetrics().elidedText(m_title, Qt::ElideRight, qMax(0, width() - 20))); }
    QString m_title;
};

class TagAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new Tag(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Always replace the complete presentation when a cached widget is rebound.
        static_cast<Tag *>(widget)->setTitle(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<Tag *>(widget)->setTitle(QString());
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        // Obtain width from data without creating a widget for every model row.
        return QSize(QFontMetrics(QApplication::font()).horizontalAdvance(index.data().toString()) + 24, 32);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::WrapAutoHeightPage)
    {
        ui->setupUi(this);
        // The Designer page embeds this view in a widget-resizable QScrollArea.
        // When auto height leaves no internal scroll range, wheel events reach the outer area.
        setupExample();
    }
private:
    void setupExample()
    {
        // Paint an item base below hover and selection, leaving gaps unfilled.
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        // Model rows are item identities, independent of the packed display rows.
        model = new QStandardItemModel(0, 1, this);
        const QStringList names{QStringLiteral("Qt"), QStringLiteral("Ready"), QStringLiteral("In progress"),
                                QStringLiteral("Requires discussion"), QStringLiteral("Done")};
        for (int row = 0; row < 8; ++row)
            model->appendRow(new QStandardItem(names[row % names.size()] + QStringLiteral(" %1").arg(row + 1)));
        // The view owns the adapter. The page owns the model.
        ui->view->setAdapter(new TagAdapter, true);
        ui->view->setFixedItemHeight(36);
        ui->view->setItemWidthRange(40, 280);
        ui->view->setContentMargins(QMargins(12, 12, 12, 12));
        ui->view->setModel(model);

        // The framework fixes the content height; only the independent cap enables overflow scrolling.
        ui->view->setAutoHeightEnabled(true);
        ui->view->setAutoHeightMaximumHeight(180);
        connect(ui->adaptive, &QCheckBox::toggled, ui->view, &viv::VirtualItemView::setAutoHeightEnabled);
        connect(ui->maximum, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setAutoHeightMaximumHeight);
        connect(ui->count, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int count) {
            while (model->rowCount() > count) model->removeRow(model->rowCount() - 1);
            while (model->rowCount() < count)
                model->appendRow(new QStandardItem(QStringLiteral("Tag %1 extra").arg(model->rowCount() + 1)));
        });
        connect(ui->view, &viv::VirtualWrapView::wrapLayoutChanged, this, [this]() {
            ui->status->setText(QStringLiteral("显示行=%1；建议高度=%2；实际高度=%3")
                .arg(ui->view->displayRowCount()).arg(ui->view->sizeHint().height()).arg(ui->view->height()));
        });
    }

    std::unique_ptr<Ui::WrapAutoHeightPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace wrap_auto_height
