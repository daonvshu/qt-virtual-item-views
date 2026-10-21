#pragma once

#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtableview.h>
#include <QLabel>
#include <QResizeEvent>

namespace dragdemo {

constexpr int kTreeRowHeight = 30;
constexpr int kTableRowHeight = 28;
constexpr int kTableColumnWidth = 120;

/// 表格在单元格物化时使用 viewport 的整行区域作为拖动预览。
class DragDropTableView : public viv::VirtualTableView
{
public:
    using viv::VirtualTableView::VirtualTableView;

protected:
    /// 单元格物化的整行预览由示例选择 viewport 截图；框架只管理反馈动画。
    QWidget *dragSourceWidget(const QModelIndex &index) const override
    {
        if (materializationMode() == MaterializationMode::CellWidgets
            && selectionBehavior() == SelectionBehavior::SelectRows)
            return viewport();
        return VirtualTableView::dragSourceWidget(index);
    }

    /// 截图区域使用 viewport 坐标，包含冻结列和当前可见的滚动列。
    QRect dragPixmapRect(const QModelIndex &index) const override
    {
        if (materializationMode() == MaterializationMode::CellWidgets
            && selectionBehavior() == SelectionBehavior::SelectRows) {
            const QRect cell = cellRect(index);
            return QRect(0, cell.y(), viewport()->width(), cell.height());
        }
        return VirtualTableView::dragPixmapRect(index);
    }
};

/// 树的行控件：标题 + 副标题。
class NodeRowWidget : public QWidget
{
public:
    explicit NodeRowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_title = new QLabel(this);
        m_title->setObjectName(QStringLiteral("rowTitle"));
        m_detail = new QLabel(this);
        m_detail->setObjectName(QStringLiteral("rowDetail"));
    }

    void setTexts(const QString &title, const QString &detail)
    {
        m_title->setText(title);
        m_detail->setText(detail);
    }

    void relayout(int width)
    {
        const int titleWidth = qMin(220, qMax(80, width / 2));
        m_title->setGeometry(0, 0, titleWidth, height());
        m_detail->setGeometry(titleWidth + 8, 0, qMax(0, width - titleWidth - 12), height());
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        relayout(width());
    }

private:
    QLabel *m_title = nullptr;
    QLabel *m_detail = nullptr;
};

class NodeAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new NodeRowWidget(parent);
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<NodeRowWidget *>(widget);
        row->setTexts(index.data(Qt::DisplayRole).toString(),
                      index.data(Qt::UserRole + 1).toString());
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<NodeRowWidget *>(widget)->setTexts(QString(), QString());
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(320, kTreeRowHeight);
    }

    int created = 0;
};

/// 表格行控件：每列一个 ColumnHost，框架负责把 host 对齐到列几何。
class TableRowWidget : public QWidget
{
public:
    TableRowWidget(QWidget *parent, int columnCount)
        : QWidget(parent)
    {
        m_labels.reserve(columnCount);
        for (int column = 0; column < columnCount; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("cellLabel"));
            label->setGeometry(6, 0, kTableColumnWidth - 12, kTableRowHeight);
            m_labels.append(label);
        }
    }

    QLabel *label(int column) const { return m_labels.value(column); }

private:
    QVector<QLabel *> m_labels;
};

class TableRowAdapter : public viv::TableWidgetAdapter
{
public:
    explicit TableRowAdapter(int columnCount)
        : m_columnCount(columnCount)
    {
    }

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        ++created;
        return new TableRowWidget(parent, m_columnCount);
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column) {
            row->label(column)->setText(
                index.siblingAtColumn(column).data(Qt::DisplayRole).toString());
        }
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        auto *row = static_cast<TableRowWidget *>(widget);
        for (int column = 0; column < m_columnCount; ++column)
            row->label(column)->setText(QString());
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(kTableColumnWidth * m_columnCount, kTableRowHeight);
    }

    int created = 0;

private:
    int m_columnCount = 0;
};

/// 单元格物化时，每格独立创建与绑定；拖放的数据语义仍由视图和模型设置决定。
class TableCellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        auto *label = new QLabel(parent);
        label->setMargin(6);
        return label;
    }

    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

} // namespace dragdemo
