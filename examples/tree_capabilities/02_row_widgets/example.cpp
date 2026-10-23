#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>

namespace tree_row_widgets {

class EditorRow : public QWidget
{
public:
    explicit EditorRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    QCheckBox *enabled = nullptr;
    QLineEdit *title = nullptr;
private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 2, 8, 2);
        enabled = new QCheckBox(this);
        title = new QLineEdit(this);
        layout->addWidget(enabled);
        layout->addWidget(title, 1);
    }
};
class EditorAdapter : public viv::WidgetAdapter
{
public:
    explicit EditorAdapter(viv::VirtualTreeView &view) : m_view(view) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *row = new EditorRow(parent);
        // Connect once. Resolve the current binding when the event happens.
        QObject::connect(row->title, &QLineEdit::editingFinished, row, [this, row] {
            const QModelIndex index = m_view.indexForWidget(row);
            if (index.isValid())
                m_view.model()->setData(index, row->title->text(), Qt::EditRole);
        });
        QObject::connect(row->enabled, &QCheckBox::toggled, row, [this, row](bool checked) {
            const QModelIndex index = m_view.indexForWidget(row);
            if (index.isValid())
                m_view.model()->setData(index, checked ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        });
        return row;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<EditorRow *>(widget);
        // Rebinding must not write the old/new contents back into the model.
        const QSignalBlocker titleBlock(row->title), checkBlock(row->enabled);
        row->title->setText(index.data().toString());
        row->enabled->setChecked(index.data(Qt::CheckStateRole).toInt() == Qt::Checked);
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<EditorRow *>(widget);
        const QSignalBlocker titleBlock(row->title), checkBlock(row->enabled);
        row->title->clear();
        row->enabled->setChecked(false);
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(480, 40); }
private:
    viv::VirtualTreeView &m_view;
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
        auto *description = new QLabel(QStringLiteral("02 组合控件：编辑名称或勾选复选框，滚动复用后数据仍来自 model。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 35; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new EditorAdapter(*m_view), true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(40);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_row_widgets
