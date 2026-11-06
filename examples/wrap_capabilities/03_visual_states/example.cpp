#include "example.h"
#include "ui_visual_states.h"
#include <virtualitemviews/widgetadapter.h>
#include <QApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QPushButton>
#include <QFontMetrics>
#include <QPointer>
#include <memory>

namespace wrap_visual_states {

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::WrapVisualStatesPage)
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
        for (int row = 0; row < 70; ++row)
            model->appendRow(new QStandardItem(names[row % names.size()] + QStringLiteral(" %1").arg(row + 1)));
        // The view owns the adapter. The page owns the model.
        ui->view->setAdapter(new TagAdapter, true);
        ui->view->setFixedItemHeight(36);
        ui->view->setItemWidthRange(40, 280);
        ui->view->setContentMargins(QMargins(12, 12, 12, 12));
        ui->view->setModel(model);

        // Built-in painting supplies both hover and selection; tags stay transparent.
        ui->view->setStateBackgroundCornerRadius(10);
        ui->view->setVisualStateAnimationDuration(180);
        ui->view->setVisualStateAnimationEasingCurve(QEasingCurve::OutCubic);
        ui->view->setFrameCornerRadius(12);
        ui->view->setFrameBorderColor(QColor(QStringLiteral("#8293a8")));
        connect(ui->states, &QCheckBox::toggled, ui->view, &viv::VirtualWrapView::setVisualStateBackgroundVisible);
        connect(ui->radius, QOverload<int>::of(&QSpinBox::valueChanged), ui->view, &viv::VirtualWrapView::setStateBackgroundCornerRadius);
        connect(ui->colors, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
            ui->view->setStyleSheet(mode == 2 ? QStringLiteral("#vivViewport { background-color: #ffd6e8; }") : QString());
            ui->view->setBackgroundColor(mode == 1 ? QColor(QStringLiteral("#eef4fa")) : QColor());
            ui->view->setItemBackgroundColor(QColor(mode == 1 ? QStringLiteral("#dce9ed") : QStringLiteral("#eef3f8")));
            ui->view->setHoverBackgroundColor(mode == 1 ? QColor(QStringLiteral("#c0e2fa")) : QColor());
            ui->view->setSelectedBackgroundColor(mode == 1 ? QColor(QStringLiteral("#96c6f4")) : QColor());
        });
        connect(ui->selection, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            using Mode = viv::VirtualItemView::SelectionMode;
            ui->view->setSelectionMode(value == 0 ? Mode::SingleSelection : value == 1 ? Mode::ExtendedSelection
                                      : value == 2 ? Mode::MultiSelection : Mode::NoSelection);
        });
        ui->status->setText(QStringLiteral("扩展选择使用 Shift / Ctrl；方向键按相邻显示行寻找最近的项目。"));
    }

    std::unique_ptr<Ui::WrapVisualStatesPage> ui;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace wrap_visual_states
