#include "ui_pinning.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringListModel>
#include <QTimer>

namespace list_pinning {

class EditorRow : public QWidget
{
public:
    EditorRow(int id, QWidget *parent) : QWidget(parent), instanceId(id) { setupUi(); }
    const int instanceId;
    QLabel *identity = nullptr;
    QLineEdit *editor = nullptr;

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        identity = new QLabel(this);
        editor = new QLineEdit(this);
        layout->addWidget(identity);
        layout->addWidget(editor, 1);
    }
};

class EditorAdapter : public viv::WidgetAdapter
{
public:
    explicit EditorAdapter(viv::VirtualListView &view) : m_view(view) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new EditorRow(++m_serial, parent);
        QObject::connect(row->editor, &QLineEdit::editingFinished, row, [this, row] {
            const QModelIndex index = m_view.indexForWidget(row);
            if (index.isValid() && row->editor->text() != index.data().toString())
                m_view.model()->setData(index, row->editor->text(), Qt::EditRole);
        });
        return row;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<EditorRow *>(widget);
        const QSignalBlocker blocker(row->editor);
        row->identity->setText(QStringLiteral("第 %1 行 / 控件 #%2").arg(index.row() + 1).arg(row->instanceId));
        row->editor->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<EditorRow *>(widget);
        const QSignalBlocker blocker(row->editor);
        row->identity->clear();
        row->editor->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(500, 40); }

private:
    viv::VirtualListView &m_view;
    int m_serial = 0;
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::ListPinningPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::ListPinningPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 1000; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        m_model = new QStringListModel(records, ui->view);
        // EditorAdapter commits edits to the model; pinning controls widget
        // lifetime and does not replace saving or validation of editor contents.
        ui->view->setAdapter(new EditorAdapter(*ui->view), true);
        ui->view->setUniformItemHeight(40);
        ui->view->setModel(m_model);
        // Soft diagnostic limit: warn when exceeded, never force recycling.
        // Pass 0 to disable the warning threshold.
        ui->view->setMaxPinnedItems(5);
        connect(ui->pinIndex, &QPushButton::clicked, this, [this] {
            const QModelIndex index = targetIndex();
            // Retain this item's widget even outside overscan; requesting a pin
            // schedules materialization for an offscreen item. Pair with an unpin.
            ui->view->setItemPinned(index, true);
            m_pins.insert(QPersistentModelIndex(index));
        });
        connect(ui->pinWidget, &QPushButton::clicked, this, [this] {
            const QModelIndex index = targetIndex();
            // Bring the item into view, then apply any pending layout before
            // querying its widget. Ordinary scrolling need not force a flush.
            ui->view->scrollTo(index);
            ui->view->flushPendingRelayout();
            // Lookup alone never materializes a row. pinWidget uses the widget's
            // current binding and is equivalent to pinning that index explicitly.
            if (QWidget *widget = ui->view->widgetForIndex(index)) {
                ui->view->pinWidget(widget);
                m_pins.insert(QPersistentModelIndex(index));
            }
        });
        connect(ui->unpin, &QPushButton::clicked, this, [this] {
            const QModelIndex index = targetIndex();
            // Both APIs remove the explicit retention request. Active focus,
            // IME or popup retention can still keep a row alive until input ends.
            if (QWidget *widget = ui->view->widgetForIndex(index))
                ui->view->unpinWidget(widget);
            else
                ui->view->setItemPinned(index, false);
            m_pins.remove(QPersistentModelIndex(index));
        });
        connect(ui->releaseAll, &QPushButton::clicked, this, [this] {
            // Persistent indexes track item identity if rows move. Business work
            // should release its pins on completion, cancellation and failure.
            for (const auto &index : m_pins)
                ui->view->setItemPinned(index, false);
            m_pins.clear();
        });
        connect(ui->cap, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualItemView::setMaxPinnedItems);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            // Framework pinnedWidgets also counts automatic input retention,
            // whereas m_pins records only this page's explicit requests.
            const auto stats = ui->view->stats();
            ui->status->setText(QStringLiteral("本例显式保留=%1；框架保留（含焦点等）=%2；物化=%3。软上限只诊断，不强制回收。")
                .arg(m_pins.size()).arg(stats.pinnedWidgets).arg(stats.materializedItems));
        });
        timer->start(200);
    }

    QModelIndex targetIndex() const
    {
        return m_model->index(ui->row->value() - 1, 0);
    }

    QStringListModel *m_model = nullptr;
    QSet<QPersistentModelIndex> m_pins;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_pinning
