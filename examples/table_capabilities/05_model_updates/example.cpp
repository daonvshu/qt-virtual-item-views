#include "example.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace table_model_updates {

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
    explicit Example(QWidget *parent) : QWidget(parent)
    {
        setupUi();
        setupExample();
    }

private:
    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        auto *description = new QLabel(QStringLiteral("05 模型更新：修改数据、插入删除行列和重置模型，依靠 Qt model 信号自动刷新"), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *controls = new QHBoxLayout;
        m_edit = new QPushButton(QStringLiteral("修改当前格"), this);
        controls->addWidget(m_edit);
        m_add = new QPushButton(QStringLiteral("插入行"), this);
        controls->addWidget(m_add);
        m_remove = new QPushButton(QStringLiteral("删除当前行"), this);
        controls->addWidget(m_remove);
        m_column = new QPushButton(QStringLiteral("增加列"), this);
        controls->addWidget(m_column);
        m_reset = new QPushButton(QStringLiteral("重置"), this);
        controls->addWidget(m_reset);
        controls->addStretch();
        layout->addLayout(controls);
        m_view = new viv::VirtualTableView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        m_view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        m_view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        m_view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        m_view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(120, 8, m_view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        m_view->setModel(m_model);

        connect(m_edit, &QPushButton::clicked, this, [this] {
            const QModelIndex index = m_view->currentIndex();
            if (index.isValid()) m_model->setData(index, QStringLiteral("内容已更新"));
        });
        connect(m_add, &QPushButton::clicked, this, [this] {
            m_model->insertRow(0);
            m_model->setData(m_model->index(0, 0), QStringLiteral("新行"));
        });
        connect(m_remove, &QPushButton::clicked, this, [this] {
            const int row = m_view->currentIndex().row();
            if (row >= 0) m_model->removeRow(row);
        });
        connect(m_column, &QPushButton::clicked, this, [this] {
            const int c = m_model->columnCount();
            m_model->insertColumn(c);
            m_model->setHeaderData(c, Qt::Horizontal, QStringLiteral("新增列"));
        });
        connect(m_reset, &QPushButton::clicked, this, [this] {
            m_model->clear();
            m_model->setColumnCount(8);
            m_model->setRowCount(40);
            m_model->setData(m_model->index(0, 0), QStringLiteral("重置后的数据"));
        });
    }

    viv::VirtualTableView *m_view = nullptr;
    QStandardItemModel *m_model = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_column = nullptr;
    QPushButton *m_reset = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_model_updates
