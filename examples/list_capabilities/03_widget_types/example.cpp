#include "example.h"
#include <virtualitemviews/virtuallistview.h>
#include <virtualitemviews/widgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace list_widget_types {

constexpr int KindRole = Qt::UserRole + 1;
constexpr viv::WidgetType HeaderType = 1;
constexpr viv::WidgetType ContentType = 2;

class HeaderRow : public QLabel
{
public:
    explicit HeaderRow(QWidget *parent) : QLabel(parent)
    {
        setMargin(8);
        auto titleFont = font();
        titleFont.setBold(true);
        setFont(titleFont);
        setStyleSheet(QStringLiteral("background: #e5edf7;"));
    }
};

class ContentRow : public QWidget
{
public:
    explicit ContentRow(QWidget *parent) : QWidget(parent) { setupUi(); }
    QLabel *title = nullptr;
    QLabel *detail = nullptr;

private:
    void setupUi()
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(16, 0, 8, 0);
        title = new QLabel(this);
        detail = new QLabel(QStringLiteral("内容行 QWidget"), this);
        layout->addWidget(title, 1);
        layout->addWidget(detail);
    }
};

class MixedAdapter : public viv::WidgetAdapter
{
public:
    viv::WidgetType widgetType(const QModelIndex &index) const override
    {
        return index.data(KindRole).toBool() ? HeaderType : ContentType;
    }

    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override
    {
        if (type == HeaderType)
            return new HeaderRow(parent);
        return new ContentRow(parent);
    }

    void bindWidget(viv::WidgetType type, QWidget *widget, const QModelIndex &index) override
    {
        // Use the stored recycler type supplied to this callback.
        if (type == HeaderType)
            static_cast<HeaderRow *>(widget)->setText(index.data().toString());
        else
            static_cast<ContentRow *>(widget)->title->setText(index.data().toString());
    }

    void unbindWidget(viv::WidgetType type, QWidget *widget, const QModelIndex &) override
    {
        if (type == HeaderType)
            static_cast<HeaderRow *>(widget)->clear();
        else
            static_cast<ContentRow *>(widget)->title->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override { return QSize(400, 36); }
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
        layout->addWidget(new QLabel(QStringLiteral("03 多种行类型：标题和内容使用不同 QWidget 类型及复用池。"), this));
        m_view = new viv::VirtualListView(this);
        layout->addWidget(m_view, 1);
    }

    void setupExample()
    {
        auto *model = new QStandardItemModel(m_view);
        for (int group = 0; group < 20; ++group) {
            auto *title = new QStandardItem(QStringLiteral("分组 %1 · 标题行 QLabel").arg(group + 1));
            title->setData(true, KindRole);
            model->appendRow(title);
            for (int row = 0; row < 8; ++row)
                model->appendRow(new QStandardItem(QStringLiteral("条目 %1 / %2").arg(group + 1).arg(row + 1)));
        }
        // widgetType() selects the recycler pool before createWidget/bindWidget.
        // HeaderRow and ContentRow therefore never get cast across pools.
        // true makes the view own the adapter; one fixed height serves both layouts.
        m_view->setAdapter(new MixedAdapter, true);
        m_view->setUniformItemHeight(36);
        m_view->setModel(model);
    }

    viv::VirtualListView *m_view = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace list_widget_types
