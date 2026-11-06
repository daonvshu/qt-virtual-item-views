#include "example.h"
#include "ui_binding_events.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <memory>
#include "ui_card.h"
#include <QPushButton>
#include <QPointer>
namespace masonry_binding_events {
struct Record { QString title; int id = 0; bool prominent = false; QSize size; };
}
Q_DECLARE_METATYPE(masonry_binding_events::Record)

namespace masonry_binding_events {

class Card : public QWidget
{
public:
    explicit Card(bool prominent, QWidget *parent) : QWidget(parent), ui(new Ui::MasonryEventCard)
    {
        ui->setupUi(this);
        QFont font = ui->title->font();
        font.setBold(prominent);
        ui->title->setFont(font);
        // Keep arrow navigation in the item view after clicking an action.
        ui->action->setFocusPolicy(Qt::NoFocus);
    }
    std::unique_ptr<Ui::MasonryEventCard> ui;
};

class CardAdapter : public viv::WidgetAdapter
{
public:
    CardAdapter(viv::VirtualMasonryView *view, QLabel *status) : m_view(view), m_status(status) {}
    viv::WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.data(viv::kDefaultItemDataRole).value<Record>().prominent ? 1 : 0;
    }
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        auto *card = new Card(type == 1, parent);
        QObject::connect(card->ui->action, &QPushButton::clicked, card, [this, card]() {
            if (!m_view || !m_status) return;
            // A reused card can represent another model row. Resolve at event time.
            const QModelIndex index = m_view->indexForWidget(card);
            if (!index.isValid()) return;
            const Record data = index.data(viv::kDefaultItemDataRole).value<Record>();
            m_status->setText(QStringLiteral("Current binding: row=%1; id=%2; title=%3")
                .arg(index.row()).arg(data.id).arg(data.title));
        });
        return card;
    }
    void bindWidget(viv::WidgetType type, QWidget *widget, const viv::ItemBindingContext &context) override
    {
        const Record data = context.rowData.value<Record>();
        static_cast<Card *>(widget)->ui->title->setText(data.title + QStringLiteral("\nID %1 / type %2").arg(data.id).arg(type));
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const viv::ItemBindingContext &) override
    {
        auto *card = static_cast<Card *>(widget);
        card->ui->title->clear();
        card->ui->action->hide();
    }
    void visualStateChanged(viv::WidgetType, QWidget *widget, const viv::ItemBindingContext &context) override
    {
        const auto state = m_view ? m_view->visualState(context.index) : viv::VirtualItemView::VisualState{};
        // The framework paints the background; business code only toggles content.
        static_cast<Card *>(widget)->ui->action->setVisible(state.hovered || state.selected);
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        return index.data(viv::kDefaultItemDataRole).value<Record>().size;
    }
private:
    QPointer<viv::VirtualMasonryView> m_view;
    QPointer<QLabel> m_status;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryBindingEventsPage)
    {
        ui->setupUi(this);
        setupExample();
    }

private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        for (int row = 0; row < 80; ++row) {
            const Record record{QStringLiteral("Record %1").arg(row + 1), 1000 + row,
                                row % 4 == 0, QSize(160, 100 + row % 5 * 25)};
            auto *item = new QStandardItem(record.title);
            // One payload per model row; the default binding role is sufficient.
            item->setData(QVariant::fromValue(record), viv::kDefaultItemDataRole);
            model->appendRow(item);
        }
        ui->view->setAdapter(new CardAdapter(ui->view, ui->status), true);
        ui->view->setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
        ui->view->setCellWidthRange(140, 230);
        ui->view->setRowSpacing(10);
        ui->view->setColumnSpacing(10);
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        ui->view->setModel(model);

        ui->status->setText(QStringLiteral("滚动后点击卡片内的事件按钮，确认显示的是当前绑定记录。"));
    }

    std::unique_ptr<Ui::MasonryBindingEventsPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_binding_events
