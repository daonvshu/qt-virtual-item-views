#include "ui_state_content.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>

namespace list_state_content {

constexpr int DoneRole = Qt::UserRole + 1;

class ActionRow : public QWidget
{
public:
    explicit ActionRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    QLabel *title = nullptr;
    QCheckBox *done = nullptr;
    QPushButton *action = nullptr;

    void setState(const viv::VirtualItemView::VisualState &state)
    {
        // Visibility follows the target state immediately, even while background
        // progress would still be fading. Selection keeps the actions visible.
        const bool visible = state.hovered || state.selected;
        done->setVisible(visible);
        action->setVisible(visible);
    }

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        title = new QLabel(this);
        done = new QCheckBox(QStringLiteral("完成"), this);
        action = new QPushButton(QStringLiteral("标记"), this);
        // Reserve the controls' space while hidden to keep the title stable.
        for (QWidget *widget : {static_cast<QWidget *>(done), static_cast<QWidget *>(action)}) {
            auto policy = widget->sizePolicy();
            policy.setRetainSizeWhenHidden(true);
            widget->setSizePolicy(policy);
        }
        layout->addWidget(title, 1);
        layout->addWidget(done);
        layout->addWidget(action);
    }
};

class ActionAdapter : public viv::WidgetAdapter
{
public:
    explicit ActionAdapter(viv::VirtualListView &view) : m_view(view) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new ActionRow(parent);
        QObject::connect(row->done, &QCheckBox::toggled, row, [this, row](bool done) {
            const QModelIndex index = m_view.indexForWidget(row);
            if (index.isValid())
                m_view.model()->setData(index, done, DoneRole);
        });
        QObject::connect(row->action, &QPushButton::clicked, row, [this, row] {
            const QModelIndex index = m_view.indexForWidget(row);
            if (index.isValid())
                m_view.model()->setData(index, index.data().toString() + QStringLiteral(" *"));
        });
        return row;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<ActionRow *>(widget);
        const QSignalBlocker blocker(row->done);
        row->title->setText(index.data().toString());
        row->done->setChecked(index.data(DoneRole).toBool());
    }
    void visualStateChanged(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Hover/selection is view state; the checkbox's DoneRole remains model data.
        static_cast<ActionRow *>(widget)->setState(m_view.visualState(index));
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<ActionRow *>(widget);
        const QSignalBlocker blocker(row->done);
        row->title->clear();
        row->done->setChecked(false);
        // A pooled widget must not retain the old row's visible operation area.
        row->setState({});
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(500, 40); }

private:
    viv::VirtualListView &m_view;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListStateContentPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListStateContentPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        auto *model = new QStandardItemModel(ui->view);
        for (int row = 0; row < 100; ++row)
            model->appendRow(new QStandardItem(QStringLiteral("条目 %1").arg(row + 1)));
        // visualStateChanged() controls whether the row's action widgets are visible.
        ui->view->setAdapter(new ActionAdapter(*ui->view), true);
        ui->view->setUniformItemHeight(40);
        ui->view->setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
        ui->view->setModel(model);
    }

};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_state_content
