#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStringListModel>
#include <QVBoxLayout>

namespace list_selection {

class StateRow : public QLabel
{
public:
    explicit StateRow(QWidget *parent) : QLabel(parent) { setMargin(8); }

    void setState(const viv::VirtualItemView::VisualState &state,
                  const QColor &hover, const QColor &selected)
    {
        m_state = state;
        m_hover = hover;
        m_selected = selected;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setOpacity(m_state.hoverProgress);
            painter.setBrush(m_hover);
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
            painter.setOpacity(m_state.selectedProgress);
            painter.setBrush(m_selected);
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
        }
        QLabel::paintEvent(event);
    }

private:
    viv::VirtualItemView::VisualState m_state;
    QColor m_hover;
    QColor m_selected;
};

class StateAdapter : public viv::WidgetAdapter
{
public:
    explicit StateAdapter(viv::VirtualListView &view) : m_view(view) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new StateRow(parent);
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<StateRow *>(widget)->setText(index.data().toString());
    }

    void visualStateChanged(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<StateRow *>(widget)->setState(m_view.visualState(index),
            m_view.hoverBackgroundColor(), m_view.selectedBackgroundColor());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<StateRow *>(widget);
        row->clear();
        row->setState({}, {}, {});
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(400, 36);
    }

private:
    viv::VirtualListView &m_view;
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
        auto *intro = new QLabel(QStringLiteral("10 选择：两个列表共享同一模型和选择模型。点击、Ctrl、Shift 或键盘操作可观察同步。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *controls = new QHBoxLayout;
        m_mode = new QComboBox(this);
        m_mode->addItems({QStringLiteral("不选择"), QStringLiteral("单选"), QStringLiteral("多选"), QStringLiteral("扩展选择")});
        m_select = new QPushButton(QStringLiteral("程序选择第 2–5 行"), this);
        m_clear = new QPushButton(QStringLiteral("清空选择"), this);
        controls->addWidget(m_mode);
        controls->addWidget(m_select);
        controls->addWidget(m_clear);
        layout->addLayout(controls);
        auto *views = new QHBoxLayout;
        m_left = new viv::VirtualListView(this);
        m_right = new viv::VirtualListView(this);
        views->addWidget(m_left);
        views->addWidget(m_right);
        layout->addLayout(views, 1);
        m_status = new QLabel(this);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 100; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        m_model = new QStringListModel(records, this);
        m_selection = new QItemSelectionModel(m_model, this);
        for (auto *view : {m_left, m_right}) {
            // Both views use the same model and selection model, so selecting in
            // either one updates the other without duplicating selection state.
            // This example paints state inside its row widget.
            view->setVisualStateBackgroundVisible(false);
            view->setAdapter(new StateAdapter(*view), true);
            view->setUniformItemHeight(36);
            view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
            view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
            view->setModel(m_model);
            // Install the shared selection model after setModel(); it must
            // refer to the exact model used by both views.
            view->setSelectionModel(m_selection);
        }
        connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            using Mode = viv::VirtualItemView::SelectionMode;
            const Mode modes[] = {Mode::NoSelection, Mode::SingleSelection, Mode::MultiSelection, Mode::ExtendedSelection};
            // SelectionMode controls mouse/keyboard gestures; it does not prevent
            // direct programmatic changes through QItemSelectionModel.
            m_left->setSelectionMode(modes[mode]);
            m_right->setSelectionMode(modes[mode]);
        });
        connect(m_select, &QPushButton::clicked, this, [this] {
            const QItemSelection range(m_model->index(1, 0), m_model->index(4, 0));
            // Replace the previous selection with this inclusive range.
            m_selection->select(range, QItemSelectionModel::ClearAndSelect);
            // Update the shared current index without changing the selected range.
            // The view's setCurrentIndex() would replace it in selecting modes.
            m_selection->setCurrentIndex(m_model->index(4, 0), QItemSelectionModel::NoUpdate);
            // scrollTo uses the default EnsureVisible hint here.
            m_left->scrollTo(m_model->index(4, 0));
        });
        connect(m_clear, &QPushButton::clicked, m_selection, &QItemSelectionModel::clearSelection);
        connect(m_selection, &QItemSelectionModel::selectionChanged, this, [this] { updateStatus(); });
        connect(m_selection, &QItemSelectionModel::currentChanged, this, [this] { updateStatus(); });
        m_mode->setCurrentIndex(3);
        updateStatus();
    }

    void updateStatus()
    {
        m_status->setText(QStringLiteral("当前行：%1；已选择：%2 行。选择模式约束用户手势，程序仍可直接操作选择模型。")
            .arg(m_selection->currentIndex().isValid() ? m_selection->currentIndex().row() + 1 : 0)
            .arg(m_selection->selectedIndexes().size()));
    }

    viv::VirtualListView *m_left = nullptr;
    viv::VirtualListView *m_right = nullptr;
    QStringListModel *m_model = nullptr;
    QItemSelectionModel *m_selection = nullptr;
    QComboBox *m_mode = nullptr;
    QPushButton *m_select = nullptr;
    QPushButton *m_clear = nullptr;
    QLabel *m_status = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_selection
