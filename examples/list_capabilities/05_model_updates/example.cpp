#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QAbstractListModel>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QVBoxLayout>

namespace list_model_updates {

class RecordModel : public QAbstractListModel
{
public:
    explicit RecordModel(QObject *parent) : QAbstractListModel(parent) { resetRecords(); }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_records.size());
    }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_records.size())
            return {};
        return role == Qt::DisplayRole || role == Qt::EditRole ? QVariant(m_records.at(index.row())) : QVariant();
    }
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override
    {
        if (!index.isValid() || index.model() != this || index.row() >= m_records.size()
            || (role != Qt::DisplayRole && role != Qt::EditRole))
            return false;
        m_records[index.row()] = value.toString();
        // Updating storage alone is not observable by the view or proxy model.
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    void appendRecord()
    {
        const int row = int(m_records.size());
        // Announce structural changes around the mutation so persistent indexes,
        // selection and virtualization can follow the model's new structure.
        beginInsertRows({}, row, row);
        m_records.append(QStringLiteral("新增记录 %1").arg(++m_serial));
        endInsertRows();
    }
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (parent.isValid() || row < 0 || count <= 0 || row > m_records.size()
            || count > m_records.size() - row)
            return false;
        beginRemoveRows(parent, row, row + count - 1);
        for (int i = 0; i < count; ++i)
            m_records.removeAt(row);
        endRemoveRows();
        return true;
    }
    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override
    {
        if (sourceParent.isValid() || destinationParent.isValid() || count != 1
            || sourceRow < 0 || sourceRow >= m_records.size()
            || destinationChild < 0 || destinationChild > m_records.size()
            || destinationChild == sourceRow || destinationChild == sourceRow + 1)
            return false;
        if (!beginMoveRows({}, sourceRow, sourceRow, {}, destinationChild))
            return false;
        // Qt's destination is the insertion position before the source is removed.
        m_records.move(sourceRow, destinationChild > sourceRow ? destinationChild - 1 : destinationChild);
        endMoveRows();
        return true;
    }
    void resetRecords()
    {
        beginResetModel();
        m_records.clear();
        for (int row = 0; row < 100; ++row)
            m_records.append(QStringLiteral("记录 %1").arg(row + 1, 3, 10, QChar('0')));
        m_serial = 100;
        endResetModel();
    }

private:
    QStringList m_records;
    int m_serial = 100;
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
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(400, 36);
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
        auto *intro = new QLabel(QStringLiteral("05 数据更新：选择行后修改或删除；排序和过滤由代理模型完成。"), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *actions = new QHBoxLayout;
        m_add = new QPushButton(QStringLiteral("新增"), this);
        m_edit = new QPushButton(QStringLiteral("修改当前"), this);
        m_remove = new QPushButton(QStringLiteral("删除当前"), this);
        m_move = new QPushButton(QStringLiteral("源首行移到末尾"), this);
        m_reset = new QPushButton(QStringLiteral("重置"), this);
        for (auto *button : {m_add, m_edit, m_remove, m_move, m_reset})
            actions->addWidget(button);
        layout->addLayout(actions);
        auto *filters = new QHBoxLayout;
        m_filter = new QLineEdit(this);
        m_filter->setPlaceholderText(QStringLiteral("输入过滤文本；移动源行时关闭排序可直接观察结果"));
        m_sort = new QCheckBox(QStringLiteral("按名称排序"), this);
        filters->addWidget(m_filter, 1);
        filters->addWidget(m_sort);
        layout->addLayout(filters);
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        m_model = new RecordModel(m_view);
        m_proxy = new QSortFilterProxyModel(m_view);
        m_proxy->setSourceModel(m_model);
        m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        m_proxy->setDynamicSortFilter(true);
        // View ownership keeps the adapter alive through unbind callbacks;
        // row size is configured explicitly before the proxy is assigned.
        m_view->setAdapter(new TextAdapter, true);
        m_view->setUniformItemHeight(36);
        // The view binds proxy indexes. Map currentIndex() back to the source
        // model before editing or removing source data.
        m_view->setModel(m_proxy);
        // Limit user gestures to one row; data changes still arrive through model signals.
        m_view->setSelectionMode(viv::VirtualItemView::SelectionMode::SingleSelection);
        connect(m_add, &QPushButton::clicked, m_model, &RecordModel::appendRecord);
        connect(m_reset, &QPushButton::clicked, m_model, &RecordModel::resetRecords);
        connect(m_edit, &QPushButton::clicked, this, [this] {
            const QModelIndex source = m_proxy->mapToSource(m_view->currentIndex());
            if (source.isValid())
                m_model->setData(source, source.data().toString() + QStringLiteral(" *"));
        });
        connect(m_remove, &QPushButton::clicked, this, [this] {
            const QModelIndex source = m_proxy->mapToSource(m_view->currentIndex());
            if (source.isValid())
                m_model->removeRows(source.row(), 1);
        });
        connect(m_move, &QPushButton::clicked, this, [this] {
            m_model->moveRows({}, 0, 1, {}, m_model->rowCount());
        });
        connect(m_filter, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
        connect(m_sort, &QCheckBox::toggled, this, [this](bool enabled) {
            m_proxy->sort(enabled ? 0 : -1);
        });
    }

    viv::VirtualListView *m_view = nullptr;
    RecordModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_move = nullptr;
    QPushButton *m_reset = nullptr;
    QLineEdit *m_filter = nullptr;
    QCheckBox *m_sort = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_model_updates
