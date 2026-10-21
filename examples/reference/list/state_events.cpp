// Hover/selection controls presentation. Events resolve the current reused binding.
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStringListModel>

class ActionRow : public QWidget
{
public:
    explicit ActionRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    QLabel *title = nullptr;
    QPushButton *action = nullptr;

    void setState(const viv::VirtualItemView::VisualState &state,
                  const QColor &hover, const QColor &selected)
    {
        m_state = state;
        m_hover = hover;
        m_selected = selected;
        action->setVisible(state.hovered || state.selected);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setOpacity(m_state.hoverProgress);
        painter.setBrush(m_hover);
        painter.drawRoundedRect(QRectF(rect()), 6, 6);
        painter.setOpacity(m_state.selectedProgress);
        painter.setBrush(m_selected);
        painter.drawRoundedRect(QRectF(rect()), 6, 6);
    }

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        title = new QLabel(this);
        action = new QPushButton(QStringLiteral("Mark"), this);
        layout->addWidget(title, 1);
        layout->addWidget(action);
    }

    viv::VirtualItemView::VisualState m_state;
    QColor m_hover;
    QColor m_selected;
};

class ActionAdapter : public viv::WidgetAdapter
{
public:
    explicit ActionAdapter(viv::VirtualListView &view) : m_view(view) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new ActionRow(parent);
        // Connect once. Never capture an index from an earlier bind.
        QObject::connect(row->action, &QPushButton::clicked, row, [this, row]() {
            const QModelIndex current = m_view.indexForWidget(row);
            if (current.isValid() && m_view.model())
                m_view.model()->setData(current, current.data().toString() + QStringLiteral(" *"));
        });
        return row;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<ActionRow *>(widget)->title->setText(index.data().toString());
    }

    void visualStateChanged(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<ActionRow *>(widget)->setState(m_view.visualState(index),
            m_view.hoverBackgroundColor(), m_view.selectedBackgroundColor());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<ActionRow *>(widget);
        row->title->clear();
        row->setState({}, {}, {});
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(320, 40);
    }

private:
    viv::VirtualListView &m_view;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStringList records;
    for (int row = 0; row < 100; ++row)
        records.append(QStringLiteral("Record %1").arg(row));
    QStringListModel model(records);
    // Transfer adapter ownership so it lives through the view's teardown callbacks.
    viv::VirtualListView view;
    // This example paints state inside its row widget.
    view.setVisualStateBackgroundVisible(false);
    view.setAdapter(new ActionAdapter(view), true);
    view.setUniformItemHeight(40);
    view.setModel(&model);
    view.setWindowTitle(QStringLiteral("List: hover, selection and row events"));
    view.resize(520, 360);
    view.show();
    return app.exec();
}
