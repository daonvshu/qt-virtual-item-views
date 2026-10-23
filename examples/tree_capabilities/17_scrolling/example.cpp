#include "example.h"
#include <QAbstractItemModel>
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace tree_scrolling {

// Generate data on demand instead of allocating a million QStandardItems.
// internalId 0 denotes a root; a child's internalId stores its parent root + 1.
class LargeTreeModel : public QAbstractItemModel
{
public:
    explicit LargeTreeModel(QObject *parent) : QAbstractItemModel(parent) {}
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (!hasIndex(row, column, parent))
            return {};
        return createIndex(row, column, parent.isValid() ? quintptr(parent.row() + 1) : quintptr(0));
    }
    QModelIndex parent(const QModelIndex &index) const override
    {
        return index.isValid() && index.internalId() != 0
            ? createIndex(int(index.internalId()) - 1, 0, quintptr(0)) : QModelIndex();
    }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (!parent.isValid())
            return 5;
        return parent.column() == 0 && parent.internalId() == 0 ? 200000 : 0;
    }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return 1; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        return index.internalId() == 0 ? QStringLiteral("分组 %1").arg(index.row() + 1)
            : QStringLiteral("节点 %1.%2").arg(index.internalId()).arg(index.row() + 1);
    }
};

class TextAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Every bind replaces the previous node's content, including after scrolling.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(480, 36);
    }
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
        auto *description = new QLabel(QStringLiteral("17 滚动：模型按需提供百万节点，每组 20 万叶子；展开路径后再定位到目标节点。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        group = new QSpinBox(this);
        group->setRange(1, 5);
        group->setValue(1);
        group->setPrefix(QStringLiteral("分组 "));
        controls->addWidget(group);
        node = new QSpinBox(this);
        node->setRange(1, 200000);
        node->setValue(100000);
        node->setPrefix(QStringLiteral("子节点 "));
        controls->addWidget(node);
        hint = new QComboBox(this);
        hint->addItems({QStringLiteral("EnsureVisible"), QStringLiteral("PositionAtTop"), QStringLiteral("PositionAtBottom"), QStringLiteral("PositionAtCenter")});
        controls->addWidget(hint);
        go = new QPushButton(QStringLiteral("定位"), this);
        controls->addWidget(go);
        controls->addStretch();
        layout->addLayout(controls);
        auto *options = new QHBoxLayout;
        step = new QPushButton(QStringLiteral("下移 120px"), this);
        options->addWidget(step);
        end = new QPushButton(QStringLiteral("到底部"), this);
        options->addWidget(end);
        wheel = new QCheckBox(QStringLiteral("滚轮按项"), this);
        wheel->setChecked(false);
        options->addWidget(wheel);
        options->addStretch();
        layout->addLayout(options);
        m_view = new viv::VirtualTreeView(this);
        layout->addWidget(m_view, 1);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
    }

    void setupExample()
    {
        auto *model = new LargeTreeModel(m_view);
        m_view->setAdapter(new TextAdapter, true);
        m_view->setUniformItemHeight(36);
        // Wheel units remain logical pixels even with very large scroll ranges.
        m_view->setWheelScrollPixels(48);
        m_view->setModel(model);
        m_view->expand(model->index(0, 0));
        connect(go, &QPushButton::clicked, this, [this, model] {
            const QModelIndex parent = model->index(group->value() - 1, 0);
            // scrollTo does not open arbitrary ancestors; expand the required path first.
            m_view->expand(parent);
            const QModelIndex index = model->index(node->value() - 1, 0, parent);
            m_view->scrollTo(index, static_cast<viv::VirtualItemView::ScrollHint>(hint->currentIndex()));
            m_view->setCurrentIndex(index);
        });
        connect(step, &QPushButton::clicked, this, [this] { m_view->scrollByPixels(120); });
        connect(end, &QPushButton::clicked, this, [this] {
            m_view->setVerticalOffset(m_view->maximumVerticalOffset());
        });
        connect(wheel, &QCheckBox::toggled, this, [this](bool items) {
            if (items)
                m_view->setWheelScrollItems(1);
            else
                m_view->setWheelScrollMode(viv::VirtualItemView::WheelScrollMode::Pixels);
        });
        connect(m_view, &viv::VirtualItemView::virtualizationUpdated, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            m_status->setText(QStringLiteral("offset=%1 / extent=%2 / maximum=%3 / 可见映射节点=%4 / 当前映射行=%5")
                .arg(m_view->verticalOffset()).arg(m_view->contentExtent()).arg(m_view->maximumVerticalOffset())
                .arg(m_view->visibleRowCount()).arg(m_view->visibilityIndex()->visibleRowForIndex(index)));
        });
    }

    viv::VirtualTreeView *m_view = nullptr;
    QLabel *m_status = nullptr;
    QSpinBox *group = nullptr;
    QSpinBox *node = nullptr;
    QComboBox *hint = nullptr;
    QPushButton *go = nullptr;
    QPushButton *step = nullptr;
    QPushButton *end = nullptr;
    QCheckBox *wheel = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_scrolling
