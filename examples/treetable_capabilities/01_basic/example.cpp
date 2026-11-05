#include "ui_basic.h"
#include <memory>
#include "example.h"
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>

namespace treetable_basic {

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(8);
        return label;
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        // Replace old content on every bind; a widget can be reused for a different row or column.
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TreetableBasicPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TreetableBasicPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        ui->view->setMaterializationMode(viv::VirtualTreeTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(ui->view);
        m_model->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("状态")});
        for (int group = 0; group < 4; ++group) {
            QList<QStandardItem *> root{new QStandardItem(QStringLiteral("分组 %1").arg(group + 1)),
                new QStandardItem(QStringLiteral("分组")), new QStandardItem(QStringLiteral("就绪"))};
            for (int child = 0; child < 5; ++child) {
                // Children belong to column zero; each child still contains all three logical columns.
                root.first()->appendRow({new QStandardItem(QStringLiteral("节点 %1.%2").arg(group + 1).arg(child + 1)),
                    new QStandardItem(QStringLiteral("记录")), new QStandardItem(QStringLiteral("正常"))});
            }
            m_model->appendRow(root);
        }
        // Model signals maintain visible nodes automatically; no flattened proxy is needed.
        ui->view->setModel(m_model);
        ui->view->setColumnWidth(0, 280);
        // Expansion takes a model node index, not a visible-row number.
        ui->view->expand(m_model->index(0, 0));
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace treetable_basic
