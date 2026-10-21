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
#include <QVBoxLayout>

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel(QStringLiteral("17 屏外保留：保留某行后滚动离开再返回，控件编号保持不变。编辑器持有焦点时，框架也会暂时保留它。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *target = new QHBoxLayout;
        m_row = new QSpinBox(this);
        m_row->setRange(1, 1000);
        m_row->setPrefix(QStringLiteral("第 "));
        m_row->setSuffix(QStringLiteral(" 行"));
        m_pinIndex = new QPushButton(QStringLiteral("按 index 保留"), this);
        m_pinWidget = new QPushButton(QStringLiteral("定位并按 widget 保留"), this);
        target->addWidget(m_row);
        target->addWidget(m_pinIndex);
        target->addWidget(m_pinWidget);
        layout->addLayout(target);
        auto *release = new QHBoxLayout;
        m_unpin = new QPushButton(QStringLiteral("释放指定行"), this);
        m_releaseAll = new QPushButton(QStringLiteral("释放全部显式保留"), this);
        m_cap = new QSpinBox(this);
        m_cap->setRange(0, 50);
        m_cap->setValue(5);
        m_cap->setPrefix(QStringLiteral("诊断软上限 "));
        release->addWidget(m_unpin);
        release->addWidget(m_releaseAll);
        release->addWidget(m_cap);
        layout->addLayout(release);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        QStringList records;
        for (int row = 0; row < 1000; ++row)
            records.append(QStringLiteral("条目 %1").arg(row + 1));
        m_model = new QStringListModel(records, m_view);
        // EditorAdapter commits edits to the model; pinning controls widget
        // lifetime and does not replace saving or validation of editor contents.
        m_view->setAdapter(new EditorAdapter(*m_view), true);
        m_view->setUniformItemHeight(40);
        m_view->setModel(m_model);
        // Soft diagnostic limit: warn when exceeded, never force recycling.
        // Pass 0 to disable the warning threshold.
        m_view->setMaxPinnedItems(5);
        connect(m_pinIndex, &QPushButton::clicked, this, [this] {
            const QModelIndex index = targetIndex();
            // Retain this item's widget even outside overscan; requesting a pin
            // schedules materialization for an offscreen item. Pair with an unpin.
            m_view->setItemPinned(index, true);
            m_pins.insert(QPersistentModelIndex(index));
        });
        connect(m_pinWidget, &QPushButton::clicked, this, [this] {
            const QModelIndex index = targetIndex();
            // Bring the item into view, then apply any pending layout before
            // querying its widget. Ordinary scrolling need not force a flush.
            m_view->scrollTo(index);
            m_view->flushPendingRelayout();
            // Lookup alone never materializes a row. pinWidget uses the widget's
            // current binding and is equivalent to pinning that index explicitly.
            if (QWidget *widget = m_view->widgetForIndex(index)) {
                m_view->pinWidget(widget);
                m_pins.insert(QPersistentModelIndex(index));
            }
        });
        connect(m_unpin, &QPushButton::clicked, this, [this] {
            const QModelIndex index = targetIndex();
            // Both APIs remove the explicit retention request. Active focus,
            // IME or popup retention can still keep a row alive until input ends.
            if (QWidget *widget = m_view->widgetForIndex(index))
                m_view->unpinWidget(widget);
            else
                m_view->setItemPinned(index, false);
            m_pins.remove(QPersistentModelIndex(index));
        });
        connect(m_releaseAll, &QPushButton::clicked, this, [this] {
            // Persistent indexes track item identity if rows move. Business work
            // should release its pins on completion, cancellation and failure.
            for (const auto &index : m_pins)
                m_view->setItemPinned(index, false);
            m_pins.clear();
        });
        connect(m_cap, QOverload<int>::of(&QSpinBox::valueChanged), m_view, &viv::VirtualItemView::setMaxPinnedItems);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            // Framework pinnedWidgets also counts automatic input retention,
            // whereas m_pins records only this page's explicit requests.
            const auto stats = m_view->stats();
            m_status->setText(QStringLiteral("本例显式保留=%1；框架保留（含焦点等）=%2；物化=%3。软上限只诊断，不强制回收。")
                .arg(m_pins.size()).arg(stats.pinnedWidgets).arg(stats.materializedItems));
        });
        timer->start(200);
    }

    QModelIndex targetIndex() const
    {
        return m_model->index(m_row->value() - 1, 0);
    }

    viv::VirtualListView *m_view = nullptr;
    QStringListModel *m_model = nullptr;
    QSet<QPersistentModelIndex> m_pins;
    QSpinBox *m_row = nullptr;
    QSpinBox *m_cap = nullptr;
    QPushButton *m_pinIndex = nullptr;
    QPushButton *m_pinWidget = nullptr;
    QPushButton *m_unpin = nullptr;
    QPushButton *m_releaseAll = nullptr;
    QLabel *m_status = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_pinning
