#include "example.h"
#include "ui_binding_events.h"
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QPushButton>
#include <QFontMetrics>
#include <QPointer>
#include <memory>

struct WrapRecord { int id; QString title; bool action; };
Q_DECLARE_METATYPE(WrapRecord)
namespace wrap_binding_events {

class RecordAdapter : public viv::WidgetAdapter
{
public:
    explicit RecordAdapter(viv::VirtualWrapView *view, QLabel *status) : m_view(view), m_status(status) {}
    viv::WidgetType widgetType(const QModelIndex &index) const override
    { return index.data(viv::kDefaultItemDataRole).value<WrapRecord>().action ? 1 : 0; }
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (!type) return new QLabel(parent);
        auto *button = new QPushButton(parent);
        button->setFocusPolicy(Qt::NoFocus);
        // Keep the view's item base and animated state layers visible.
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: 1px solid #d4dfe9; padding: 0 8px; }"));
        const QPointer<viv::VirtualWrapView> view = m_view;
        const QPointer<QLabel> status = m_status;
        QObject::connect(button, &QPushButton::clicked, button, [view, status, button]() {
            if (!view || !status) return;
            // Resolve at event time: persistent indexes track model moves, not widget reuse.
            const QModelIndex index = view->indexForWidget(button);
            if (!index.isValid()) return;
            const WrapRecord record = index.data(viv::kDefaultItemDataRole).value<WrapRecord>();
            status->setText(QStringLiteral("当前模型行=%1；ID=%2；内容=%3").arg(index.row()).arg(record.id).arg(record.title));
        });
        return button;
    }
    void bindWidget(viv::WidgetType type, QWidget *widget, const viv::ItemBindingContext &context) override
    {
        const WrapRecord record = context.rowData.value<WrapRecord>();
        if (type) static_cast<QPushButton *>(widget)->setText(record.title);
        else static_cast<QLabel *>(widget)->setText(record.title);
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        const WrapRecord record = index.data(viv::kDefaultItemDataRole).value<WrapRecord>();
        return QSize(QFontMetrics(QApplication::font()).horizontalAdvance(record.title) + 32, 36);
    }
private:
    QPointer<viv::VirtualWrapView> m_view;
    QPointer<QLabel> m_status;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::WrapBindingEventsPage)
    {
        ui->setupUi(this);
        setupExample();
    }
private:
    void setupExample()
    {
        // Paint an item base below hover and selection, leaving gaps unfilled.
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        model = new QStandardItemModel(100, 1, this);
        for (int row = 0; row < model->rowCount(); ++row) {
            WrapRecord record{row + 1000, QStringLiteral("Record %1").arg(row + 1), row % 3 == 0};
            model->setData(model->index(row, 0), QVariant::fromValue(record), viv::kDefaultItemDataRole);
        }
        // Context-aware bindWidget receives the complete payload, including after reuse.
        ui->view->setItemDataRole(viv::kDefaultItemDataRole);
        ui->view->setAdapter(new RecordAdapter(ui->view, ui->status), true);
        ui->view->setFixedItemHeight(36);
        ui->view->setModel(model);
        connect(ui->change, &QPushButton::clicked, this, [this]() {
            WrapRecord record{1000, QStringLiteral("Updated complete record"), true};
            model->setData(model->index(0, 0), QVariant::fromValue(record), viv::kDefaultItemDataRole);
        });
    }

    std::unique_ptr<Ui::WrapBindingEventsPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace wrap_binding_events
