#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QSortFilterProxyModel>

namespace tree_sorting_filtering {

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
        auto *description = new QLabel(QStringLiteral("21 代理模型：过滤后保留匹配节点的祖先，排序只改变兄弟节点顺序。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        filter = new QLineEdit(this);
        filter->setPlaceholderText(QStringLiteral("输入节点名称，例如 1.2"));
        controls->addWidget(filter);
        order = new QComboBox(this);
        order->addItems({QStringLiteral("升序"), QStringLiteral("降序")});
        controls->addWidget(order);
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
        for (int group = 0; group < 8; ++group) {
            auto *branch = new QStandardItem(QStringLiteral("分组 %1").arg(group + 1));
            m_model->appendRow(branch);
            for (int row = 0; row < 20; ++row) {
                auto *node = new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(row + 1));
                branch->appendRow(node);
                if (row % 3 == 0) {
                    for (int leaf = 0; leaf < 3; ++leaf)
                        node->appendRow(new QStandardItem(QStringLiteral("叶子 %1.%2.%3")
                            .arg(group + 1).arg(row + 1).arg(leaf + 1)));
                }
            }
        }

        auto *proxy = new QSortFilterProxyModel(m_view);
        proxy->setSourceModel(m_model);
        proxy->setFilterKeyColumn(0);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        // Available since Qt 5.10: keep ancestors when descendants match the text.
        proxy->setRecursiveFilteringEnabled(true);
        m_view->setAdapter(new TextAdapter, true);
        m_view->setUniformItemHeight(36);
        // All indexes passed to the view must belong to this proxy, not the source model.
        m_view->setModel(proxy);
        connect(filter, &QLineEdit::textChanged, this, [this, proxy](const QString &text) {
            proxy->setFilterFixedString(text);
            // Reveal the matching descendants so the filter result is immediately visible.
            for (int row = 0; row < proxy->rowCount(); ++row)
                m_view->expandRecursively(proxy->index(row, 0));
        });
        connect(order, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [proxy](int value) {
            proxy->sort(0, value == 0 ? Qt::AscendingOrder : Qt::DescendingOrder);
        });
        connect(m_view, &viv::VirtualItemView::clicked, this, [this, proxy](const QModelIndex &index) {
            const QModelIndex source = proxy->mapToSource(index);
            m_status->setText(QStringLiteral("proxy=%1 / source.row=%2 / source.parent=%3")
                .arg(index.data().toString()).arg(source.row()).arg(source.parent().data().toString()));
        });
        proxy->sort(0, Qt::AscendingOrder);
        m_view->expand(proxy->index(0, 0));
    }

    viv::VirtualTreeView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_status = nullptr;
    QLineEdit *filter = nullptr;
    QComboBox *order = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_sorting_filtering
