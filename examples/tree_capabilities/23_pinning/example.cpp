#include "example.h"
#include <QPersistentModelIndex>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>

namespace tree_pinning {

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
class PinAdapter : public viv::WidgetAdapter
{
public:
    explicit PinAdapter(viv::VirtualTreeView &view) : m_view(view) {}
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
        row->title->setPlaceholderText(QStringLiteral("控件实例 %1").arg(++m_serial));
        row->setProperty("instance", m_serial);
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
        auto *description = new QLabel(QStringLiteral("23 屏外保留：选择节点后保留控件，再滚出屏幕查询实例编号；数据仍应写回 model。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        pin = new QPushButton(QStringLiteral("按 index 保留当前项"), this);
        controls->addWidget(pin);
        widgetPin = new QPushButton(QStringLiteral("按 widget 保留"), this);
        controls->addWidget(widgetPin);
        unpin = new QPushButton(QStringLiteral("释放目标保留"), this);
        controls->addWidget(unpin);
        query = new QPushButton(QStringLiteral("查询目标实例"), this);
        controls->addWidget(query);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(m_view);
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 100; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        m_view->setAdapter(new PinAdapter(*m_view), true);
        // Uniform height avoids measuring each materialized node.
        m_view->setUniformItemHeight(40);
        // Qt model signals update the visible mapping and reused widgets automatically.
        m_view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        m_view->expand(m_model->index(0, 0));

        // A soft diagnostic limit warns about excessive pins; it never force-recycles them.
        m_view->setMaxPinnedItems(8);
        connect(pin, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            if (!index.isValid())
                return;
            if (m_target.isValid() && m_target != index)
                m_view->setItemPinned(m_target, false);
            m_target = QPersistentModelIndex(index);
            // Explicit pin retains a visible-mapping node even outside the overscan window.
            m_view->setItemPinned(index, true);
        });
        connect(widgetPin, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            if (QWidget *widget = m_view->widgetForIndex(index)) {
                if (m_target.isValid() && m_target != index)
                    m_view->setItemPinned(m_target, false);
                m_target = QPersistentModelIndex(index);
                m_view->pinWidget(widget);
            }
        });
        connect(unpin, &QPushButton::clicked, this, [this] {
            // Explicit retention is independent of temporary focus/popup/drag retention.
            if (m_target.isValid())
                m_view->setItemPinned(m_target, false);
        });
        connect(query, &QPushButton::clicked, this, [this] {
            QWidget *widget = m_view->widgetForIndex(m_target);
            m_status->setText(QStringLiteral("目标=%1 / 映射可见=%2 / pinned=%3 / 控件实例=%4 / 当前保留=%5")
                .arg(m_target.data().toString()).arg(m_view->visibilityIndex()->isVisible(m_target))
                .arg(m_view->isItemPinned(m_target)).arg(widget ? widget->property("instance").toInt() : -1)
                .arg(m_view->pinnedItemCount()));
        });
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *pin = nullptr;
    QPushButton *widgetPin = nullptr;
    QPushButton *unpin = nullptr;
    QPushButton *query = nullptr;
    QPersistentModelIndex m_target;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_pinning
