#include "example.h"
#include "ui_scrolling.h"
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QPushButton>
#include <QFontMetrics>
#include <QPointer>
#include <memory>
#include <QAbstractListModel>

namespace wrap_scrolling {

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

class TagModel : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 10000; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole) return {};
        return QStringLiteral("Item %1 ").arg(index.row() + 1) + QString((index.row() % 8) + 1, QLatin1Char('x'));
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::WrapScrollingPage)
    {
        ui->setupUi(this);
        setupExample();
    }
private:
    void setupExample()
    {
        // Paint an item base below hover and selection, leaving gaps unfilled.
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        auto *model = new TagModel(this);
        ui->view->setAdapter(new TagAdapter, true);
        ui->view->setFixedItemHeight(36);
        // Overscan is expressed in model items; visible display rows are always complete.
        ui->view->setOverscan(4, 8);
        ui->view->setModel(model);
        connect(ui->locate, &QPushButton::clicked, this, [this, model]() {
            ui->view->scrollTo(model->index(ui->target->value() - 1, 0), viv::VirtualItemView::PositionAtCenter);
        });
        connect(ui->pin, &QCheckBox::toggled, this, [this, model](bool checked) {
            ui->view->setItemPinned(model->index(0, 0), checked);
        });
        // The framework aggregates enabled probes periodically; filter viv.performance.*.
        connect(ui->logging, &QCheckBox::toggled, ui->view, &viv::VirtualItemView::setPerformanceLoggingEnabled);
        connect(ui->view, &viv::VirtualItemView::virtualizationUpdated, this, [this]() {
            ui->status->setText(QStringLiteral("物化=%1 / 缓存=%2 / 创建累计=%3；显示行数=%4")
                .arg(ui->view->materializedItemCount()).arg(ui->view->pooledWidgetCount())
                .arg(ui->view->createdWidgetCount()).arg(ui->view->displayRowCount()));
        });
    }

    std::unique_ptr<Ui::WrapScrollingPage> ui;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace wrap_scrolling
