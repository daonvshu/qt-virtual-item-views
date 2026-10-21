#pragma once

#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/virtualtableview.h>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>

namespace dragdemo {

constexpr int kTreeRowHeight = 30;
constexpr int kTableRowHeight = 28;
constexpr int kTableColumnWidth = 120;

/// 表格使用框架计算的行状态区域绘制背景，覆盖冻结列和滚动列。
class DragDropTableView : public viv::VirtualTableView
{
public:
    using viv::VirtualTableView::VirtualTableView;

protected:
    void paintStateBackgroundLayer(QPainter *painter, const QRect &extended,
                                  const QRegion &clip,
                                  const QColor &color) const override
    {
        paintRoundedStateBackgroundLayer(painter, extended, clip, color, 0);
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
    void paintEvent(QPaintEvent *) override
    {
        // 树由视图统一绘制（包括 branch 区域）；列表由行控件绘制。
        for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            auto *view = qobject_cast<viv::VirtualListView *>(ancestor);
            if (!view)
                continue;
            const auto state = view->visualState(view->indexForWidget(this));
            QPainter painter(this);
            painter.setOpacity(state.hoverProgress);
            painter.fillRect(rect(), view->hoverBackgroundColor());
            painter.setOpacity(state.selectedProgress);
            painter.fillRect(rect(), view->selectedBackgroundColor());
            break;
        }
    }

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

} // namespace dragdemo
