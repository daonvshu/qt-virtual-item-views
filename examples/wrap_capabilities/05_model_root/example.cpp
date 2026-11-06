#include "example.h"
#include "ui_model_root.h"
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QPushButton>
#include <QFontMetrics>
#include <QPointer>
#include <memory>
#include <QSortFilterProxyModel>

namespace wrap_model_root {

class Tag : public QLabel
{
public:
    explicit Tag(QWidget *parent) : QLabel(parent) { setMargin(10); }
    void setTitle(const QString &text) { m_title = text; updateText(); }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }
private:
    void updateText() { setText(fontMetrics().elidedText(m_title, Qt::ElideRight, qMax(0, width() - 20))); }
    QString m_title;
};

class TagAdapter : public viv::WidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new Tag(parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        // Always replace the complete presentation when a cached widget is rebound.
        static_cast<Tag *>(widget)->setTitle(index.data().toString());
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<Tag *>(widget)->setTitle(QString());
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        // Obtain width from data without creating a widget for every model row.
        return QSize(QFontMetrics(QApplication::font()).horizontalAdvance(index.data().toString()) + 24, 32);
    }
};

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::WrapModelRootPage)
    {
        ui->setupUi(this);
        setupExample();
    }
private:
    void setupExample()
    {
        // Paint an item base below hover and selection, leaving gaps unfilled.
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        // Model rows are item identities, independent of the packed display rows.
        model = new QStandardItemModel(0, 1, this);
        const QStringList names{QStringLiteral("Qt"), QStringLiteral("Ready"), QStringLiteral("In progress"),
                                QStringLiteral("Requires discussion"), QStringLiteral("Done")};
        for (int row = 0; row < 35; ++row)
            model->appendRow(new QStandardItem(names[row % names.size()] + QStringLiteral(" %1").arg(row + 1)));
        // The view owns the adapter. The page owns the model.
        ui->view->setAdapter(new TagAdapter, true);
        ui->view->setFixedItemHeight(36);
        ui->view->setItemWidthRange(40, 280);
        ui->view->setContentMargins(QMargins(12, 12, 12, 12));


        auto *group = new QStandardItem(QStringLiteral("Group 1"));
        for (int i = 0; i < 20; ++i) group->appendRow(new QStandardItem(QStringLiteral("Child %1").arg(i)));
        model->insertRow(0, group);
        groupIndex = model->index(0, 0);
        proxy = new QSortFilterProxyModel(this);
        proxy->setSourceModel(model);
        ui->view->setModel(proxy);
        connect(ui->add, &QPushButton::clicked, this, [this]() {
            QStandardItem *parent = ui->root->isChecked() ? model->itemFromIndex(groupIndex) : model->invisibleRootItem();
            if (parent) parent->appendRow(new QStandardItem(QStringLiteral("Added %1").arg(++sequence)));
        });
        connect(ui->remove, &QPushButton::clicked, this, [this]() {
            const QModelIndex index = proxy->mapToSource(ui->view->currentIndex());
            if (index.isValid()) model->removeRow(index.row(), index.parent());
        });
        connect(ui->rename, &QPushButton::clicked, this, [this]() {
            const QModelIndex index = proxy->mapToSource(ui->view->currentIndex());
            if (index.isValid()) model->setData(index, index.data().toString() + QStringLiteral(" extended"));
        });
        connect(ui->filter, &QCheckBox::toggled, this, [this](bool checked) {
            proxy->setFilterFixedString(checked ? QStringLiteral("1") : QString());
        });
        connect(ui->sort, &QCheckBox::toggled, this, [this](bool checked) { proxy->sort(0, checked ? Qt::DescendingOrder : Qt::AscendingOrder); });
        connect(ui->root, &QCheckBox::toggled, this, [this](bool checked) {
            // Root indexes belong to the installed proxy, not the source model.
            ui->view->setRootIndex(checked ? proxy->mapFromSource(groupIndex) : QModelIndex());
        });
    }

    std::unique_ptr<Ui::WrapModelRootPage> ui;
    QStandardItemModel *model = nullptr;
    QSortFilterProxyModel *proxy = nullptr;
    QPersistentModelIndex groupIndex;
    int sequence = 0;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace wrap_model_root
