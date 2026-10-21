#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaType>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace list_binding_context {

struct Record
{
    int id = 0;
    QString title;
    bool completed = false;
};

} // namespace list_binding_context

// Make Record usable as a QVariant payload without splitting it into many roles.
Q_DECLARE_METATYPE(list_binding_context::Record)

namespace list_binding_context {

constexpr int RecordRole = Qt::UserRole + 10;

class RecordRow : public QWidget
{
public:
    explicit RecordRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    QLabel *text = nullptr;
    QPushButton *toggle = nullptr;

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 8, 0);
        text = new QLabel(this);
        toggle = new QPushButton(QStringLiteral("切换完成状态"), this);
        layout->addWidget(text, 1);
        layout->addWidget(toggle);
    }
};

class RecordAdapter : public viv::WidgetAdapter
{
public:
    explicit RecordAdapter(viv::VirtualListView &view) : m_view(view) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new RecordRow(parent);
        QObject::connect(row->toggle, &QPushButton::clicked, row, [this, row] {
            // rowData belongs to one bind call; an event must resolve the reused
            // row's current index, read the current payload and write it back.
            const QModelIndex index = m_view.indexForWidget(row);
            if (!index.isValid())
                return;
            auto record = index.data(RecordRole).value<Record>();
            record.completed = !record.completed;
            m_view.model()->setData(index, QVariant::fromValue(record), RecordRole);
        });
        return row;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const viv::ItemBindingContext &context) override
    {
        // The complete payload arrives here; no index.data() calls are needed.
        const Record record = context.rowData.value<Record>();
        static_cast<RecordRow *>(widget)->text->setText(
            QStringLiteral("#%1  %2  [%3]  depth=%4 / column=%5")
                .arg(record.id).arg(record.title)
                .arg(record.completed ? QStringLiteral("完成") : QStringLiteral("待办"))
                .arg(context.depth).arg(context.logicalColumn));
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<RecordRow *>(widget)->text->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(600, 40); }

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
        layout->addWidget(new QLabel(QStringLiteral("04 整行数据绑定：model 每行存一份 Record，bind 直接接收 rowData。"), this));
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        auto *model = new QStandardItemModel(m_view);
        for (int row = 0; row < 100; ++row) {
            auto *item = new QStandardItem;
            item->setData(QVariant::fromValue(Record{row + 1, QStringLiteral("记录 %1").arg(row + 1), false}), RecordRole);
            model->appendRow(item);
        }
        // The adapter receives the complete row payload through ItemBindingContext.
        m_view->setAdapter(new RecordAdapter(*m_view), true);
        // rowData is read from this role using the row's canonical model index.
        // Changing the payload through model.setData() emits dataChanged and rebinds it.
        m_view->setItemDataRole(RecordRole);
        m_view->setUniformItemHeight(40);
        m_view->setModel(model);
    }

    viv::VirtualListView *m_view = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_binding_context
