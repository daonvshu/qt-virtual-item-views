#include "ui_pinning.h"
#include <memory>
#include "example.h"
#include <QPersistentModelIndex>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItemModel>
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreePinningPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreePinningPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // A plain QStandardItemModel owns the hierarchy; only column zero owns children.
        m_model = new QStandardItemModel(ui->view);
        for (int group = 0; group < 5; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 100; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
            }
        }

        // The view owns the adapter through teardown; the QObject parent owns the model.
        ui->view->setAdapter(new PinAdapter(*ui->view), true);
        // Uniform height avoids measuring each materialized node.
        ui->view->setUniformItemHeight(40);
        // Qt model signals update the visible mapping and reused widgets automatically.
        ui->view->setModel(m_model);

        // Expand through the view so mapping, branch state and signals stay synchronized.
        ui->view->expand(m_model->index(0, 0));

        // A soft diagnostic limit warns about excessive pins; it never force-recycles them.
        ui->view->setMaxPinnedItems(8);
        connect(ui->pin, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            if (!index.isValid())
                return;
            if (m_target.isValid() && m_target != index)
                ui->view->setItemPinned(m_target, false);
            m_target = QPersistentModelIndex(index);
            // Explicit pin retains a visible-mapping node even outside the overscan window.
            ui->view->setItemPinned(index, true);
        });
        connect(ui->widgetPin, &QPushButton::clicked, this, [this] {
            const QModelIndex index = ui->view->currentIndex();
            if (QWidget *widget = ui->view->widgetForIndex(index)) {
                if (m_target.isValid() && m_target != index)
                    ui->view->setItemPinned(m_target, false);
                m_target = QPersistentModelIndex(index);
                ui->view->pinWidget(widget);
            }
        });
        connect(ui->unpin, &QPushButton::clicked, this, [this] {
            // Explicit retention is independent of temporary focus/popup/drag retention.
            if (m_target.isValid())
                ui->view->setItemPinned(m_target, false);
        });
        connect(ui->query, &QPushButton::clicked, this, [this] {
            QWidget *widget = ui->view->widgetForIndex(m_target);
            ui->status->setText(QStringLiteral("目标=%1 / 映射可见=%2 / pinned=%3 / 控件实例=%4 / 当前保留=%5")
                .arg(m_target.data().toString()).arg(ui->view->visibilityIndex()->isVisible(m_target))
                .arg(ui->view->isItemPinned(m_target)).arg(widget ? widget->property("instance").toInt() : -1)
                .arg(ui->view->pinnedItemCount()));
        });
    }

    QStandardItemModel *m_model = nullptr;
    QPersistentModelIndex m_target;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_pinning
