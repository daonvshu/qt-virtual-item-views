#pragma once

#include <virtualitemviews/tablewidgetadapter.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QStringList>
#include <QVector>

namespace reference {

// ColumnHost lets the framework position, resize and clip each column.
class TextRow : public QWidget
{
public:
    explicit TextRow(int columns, QWidget *parent) : QWidget(parent)
    {
        setupUi(columns);
    }

    void bind(const QModelIndex &index)
    {
        for (int column = 0; column < m_labels.size(); ++column)
            m_labels[column]->setText(index.siblingAtColumn(column).data().toString());
    }

    void clear()
    {
        for (QLabel *label : m_labels)
            label->clear();
    }

    void setTexts(const QStringList &values)
    {
        for (int column = 0; column < m_labels.size(); ++column)
            m_labels[column]->setText(values.value(column));
    }

private:
    void setupUi(int columns)
    {
        for (int column = 0; column < columns; ++column) {
            auto *host = new viv::ColumnHost(column, this);
            auto *layout = new QHBoxLayout(host);
            layout->setContentsMargins(8, 0, 8, 0);
            auto *label = new QLabel(host);
            layout->addWidget(label);
            m_labels.append(label);
        }
    }

    QVector<QLabel *> m_labels;
};

// Fixed schema for these examples; recreate/adapt the schema if your columns change.
class TextRowAdapter : public viv::TableWidgetAdapter
{
public:
    explicit TextRowAdapter(int columns) : m_columns(columns) {}

    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new TextRow(m_columns, parent);
    }

    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<TextRow *>(widget)->bind(index);
    }

    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<TextRow *>(widget)->clear();
    }

    QSize estimatedSize(viv::WidgetType, const QModelIndex &) const override
    {
        return QSize(0, 36);
    }

private:
    int m_columns;
};

class TextCellAdapter : public viv::CellWidgetAdapter
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
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }

    void unbindCellWidget(QWidget *widget, const QModelIndex &) override
    {
        static_cast<QLabel *>(widget)->clear();
    }
};

} // namespace reference
