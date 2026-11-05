#include "ui_sorting_filtering.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QComboBox>
#include <QLabel>
#include <QStandardItemModel>
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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreeSortingFilteringPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreeSortingFilteringPage> ui;

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

        auto *proxy = new QSortFilterProxyModel(ui->view);
        proxy->setSourceModel(m_model);
        proxy->setFilterKeyColumn(0);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        // Available since Qt 5.10: keep ancestors when descendants match the text.
        proxy->setRecursiveFilteringEnabled(true);
        ui->view->setAdapter(new TextAdapter, true);
        ui->view->setUniformItemHeight(36);
        // All indexes passed to the view must belong to this proxy, not the source model.
        ui->view->setModel(proxy);
        connect(ui->filter, &QLineEdit::textChanged, this, [this, proxy](const QString &text) {
            proxy->setFilterFixedString(text);
            // Reveal the matching descendants so the filter result is immediately visible.
            for (int row = 0; row < proxy->rowCount(); ++row)
                ui->view->expandRecursively(proxy->index(row, 0));
        });
        connect(ui->order, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [proxy](int value) {
            proxy->sort(0, value == 0 ? Qt::AscendingOrder : Qt::DescendingOrder);
        });
        connect(ui->view, &viv::VirtualItemView::clicked, this, [this, proxy](const QModelIndex &index) {
            const QModelIndex source = proxy->mapToSource(index);
            ui->status->setText(QStringLiteral("proxy=%1 / source.row=%2 / source.parent=%3")
                .arg(index.data().toString()).arg(source.row()).arg(source.parent().data().toString()));
        });
        proxy->sort(0, Qt::AscendingOrder);
        ui->view->expand(proxy->index(0, 0));
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace tree_sorting_filtering
