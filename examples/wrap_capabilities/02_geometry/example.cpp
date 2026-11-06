#include "example.h"
#include "ui_geometry.h"
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QPushButton>
#include <QFontMetrics>
#include <QPointer>
#include <memory>

namespace wrap_geometry {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::WrapGeometryPage)
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
        for (int row = 0; row < 40; ++row)
            model->appendRow(new QStandardItem(names[row % names.size()] + QStringLiteral(" %1").arg(row + 1)));
        // The view owns the adapter. The page owns the model.
        ui->view->setAdapter(new TagAdapter, true);
        ui->view->setFixedItemHeight(36);
        ui->view->setItemWidthRange(40, 280);
        ui->view->setContentMargins(QMargins(12, 12, 12, 12));
        ui->view->setModel(model);

        // SizeHintRole overrides the adapter's estimate, but not setItemWidth().
        model->setData(model->index(0, 0), QSize(90, 36), Qt::SizeHintRole);
        // Horizontal gaps between items in the same wrapped row.
        ui->view->setItemSpacing(ui->itemSpacing->value());
        connect(ui->itemSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                ui->view, &viv::VirtualWrapView::setItemSpacing);
        // Vertical gaps between wrapped rows, independent of item spacing.
        ui->view->setRowSpacing(ui->rowSpacing->value());
        connect(ui->rowSpacing, QOverload<int>::of(&QSpinBox::valueChanged),
                ui->view, &viv::VirtualWrapView::setRowSpacing);
        connect(ui->height, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualWrapView::setFixedItemHeight);
        connect(ui->alignment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->setLineAlignment(value == 0 ? Qt::AlignLeft : value == 1 ? Qt::AlignHCenter : Qt::AlignRight);
        });
        connect(ui->rtl, &QCheckBox::toggled, this, [this](bool checked) {
            ui->view->setLayoutDirection(checked ? Qt::RightToLeft : Qt::LeftToRight);
        });
        connect(ui->overrideWidth, &QPushButton::clicked, this, [this]() { ui->view->setItemWidth(model->index(0, 0), 200); });
        connect(ui->clearWidth, &QPushButton::clicked, this, [this]() { ui->view->clearItemWidth(model->index(0, 0)); });
    }

    std::unique_ptr<Ui::WrapGeometryPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace wrap_geometry
