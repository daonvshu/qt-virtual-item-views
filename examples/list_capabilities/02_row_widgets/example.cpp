#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace list_row_widgets {

constexpr int DoneRole = Qt::UserRole + 1;

class EntryRow : public QWidget
{
public:
    explicit EntryRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    QCheckBox *done = nullptr;
    QLineEdit *editor = nullptr;
    QPushButton *reset = nullptr;

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 2, 8, 2);
        done = new QCheckBox(this);
        editor = new QLineEdit(this);
        reset = new QPushButton(QStringLiteral("重置名称"), this);
        layout->addWidget(done);
        layout->addWidget(editor, 1);
        layout->addWidget(reset);
    }
};

class EntryAdapter : public viv::WidgetAdapter
{
public:
    explicit EntryAdapter(viv::VirtualListView &view) : m_view(view) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new EntryRow(parent);
        // Connections belong to the widget lifetime; resolve its current binding at event time.
        QObject::connect(row->done, &QCheckBox::toggled, row, [this, row](bool checked) {
            const QModelIndex index = m_view.indexForWidget(row);
            if (index.isValid())
                m_view.model()->setData(index, checked, DoneRole);
        });
        QObject::connect(row->editor, &QLineEdit::editingFinished, row, [this, row] {
            const QModelIndex index = m_view.indexForWidget(row);
            if (index.isValid())
                m_view.model()->setData(index, row->editor->text(), Qt::EditRole);
        });
        QObject::connect(row->reset, &QPushButton::clicked, row, [this, row] {
            const QModelIndex index = m_view.indexForWidget(row);
            if (index.isValid())
                m_view.model()->setData(index, QStringLiteral("条目 %1").arg(index.row() + 1), Qt::EditRole);
        });
        return row;
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<EntryRow *>(widget);
        // Binding is not user input: block notifications while restoring model data.
        const QSignalBlocker doneSignals(row->done);
        const QSignalBlocker editorSignals(row->editor);
        row->done->setChecked(index.data(DoneRole).toBool());
        row->editor->setText(index.data().toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<EntryRow *>(widget);
        const QSignalBlocker doneSignals(row->done);
        const QSignalBlocker editorSignals(row->editor);
        row->done->setChecked(false);
        row->editor->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(500, 44); }

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
        auto *intro = new QLabel(QStringLiteral("02 自定义行控件：修改名称后按 Enter，或勾选复选框；滚动回来检查数据。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        auto *model = new QStandardItemModel(m_view);
        for (int row = 0; row < 200; ++row)
            model->appendRow(new QStandardItem(QStringLiteral("条目 %1").arg(row + 1)));
        // The adapter owns the create/bind/unbind contract for a real QWidget row.
        m_view->setAdapter(new EntryAdapter(*m_view), true);
        // The editor row has a known fixed height, so no sizeHint measurement is needed.
        m_view->setUniformItemHeight(44);
        // The model remains the source of truth for text and check state.
        m_view->setModel(model);
    }

    viv::VirtualListView *m_view = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_row_widgets
