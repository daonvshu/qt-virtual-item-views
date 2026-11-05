#include "ui_custom_header_states.h"
#include <memory>
#include "styled_headers.h"
#include <virtualitemviews/virtualtableview.h>
#include <virtualitemviews/tablewidgetadapter.h>
#include <virtualitemviews/labelheaderview.h>
#include <QStandardItemModel>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLinearGradient>
#include <QPointer>
#include <QIcon>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionHeader>

namespace table_header_states {

class GradientHeaderSection : public viv::LabelHeaderSection
{
public:
    GradientHeaderSection(QCheckBox *enabled, QWidget *parent)
        : viv::LabelHeaderSection(parent), m_enabled(enabled) {}

protected:
    QBrush visualStateBackgroundBrush(bool selected, const QColor &defaultColor) const override
    {
        // Delegate hover and the disabled example option to the built-in renderer.
        if (!selected || !m_enabled || !m_enabled->isChecked())
            return viv::StyledHeaderSection::visualStateBackgroundBrush(selected, defaultColor);

        // Section-local coordinates keep the gradient aligned after scrolling or reuse.
        // The base renderer applies selectedProgress and the configured corner geometry.
        QLinearGradient gradient(0, 0, width(), height());
        gradient.setColorAt(0.0, QColor(QStringLiteral("#7dd3fc")));
        gradient.setColorAt(0.35, QColor(QStringLiteral("#c4b5fd")));
        gradient.setColorAt(0.7, QColor(QStringLiteral("#f9a8d4")));
        gradient.setColorAt(1.0, QColor(QStringLiteral("#fde68a")));
        return QBrush(gradient);
    }

private:
    QPointer<QCheckBox> m_enabled;
};

class SelectedIconHeaderSection : public GradientHeaderSection
{
public:
    SelectedIconHeaderSection(QCheckBox *gradientEnabled, QCheckBox *iconEnabled, QWidget *parent)
        : GradientHeaderSection(gradientEnabled, parent), m_iconEnabled(iconEnabled)
    {
        // Use a built-in icon so the copied example needs no external image resources.
        m_selectedIcon = style()->standardIcon(QStyle::SP_DialogApplyButton, nullptr, this);
    }

protected:
    void paintContent(QPainter *painter) override
    {
        const int iconSize = 16;
        const int margin = 4;
        const int iconSlot = iconSize + 2 * margin;
        QStyleOptionHeader option;
        initContentStyleOption(&option);
        if (!m_iconEnabled || !m_iconEnabled->isChecked()
            || option.rect.width() <= iconSlot + 8 || option.rect.height() < iconSlot) {
            // Narrow sections and the disabled option keep the built-in text layout.
            GradientHeaderSection::paintContent(painter);
            return;
        }

        // Keep the slot reserved while enabled, including when not selected or fading out.
        // This avoids moving the label on every animation frame.
        const QRect iconRect(option.rect.left() + margin,
                             option.rect.top() + (option.rect.height() - iconSize) / 2,
                             iconSize, iconSize);
        const auto state = visualState();
        if (state.selectedProgress > 0) {
            painter->save();
            painter->setOpacity(state.selectedProgress);
            m_selectedIcon.paint(painter, iconRect, Qt::AlignCenter,
                                 isEnabled() ? QIcon::Normal : QIcon::Disabled);
            painter->restore();
        }

        // Lay out the native label and sort arrow in the remaining content area only.
        // The content helper applies padding without changing the background geometry.
        option.rect.adjust(iconSlot, 0, 0, 0);
        painter->save();
        painter->setClipRect(option.rect, Qt::IntersectClip);
        QStyleOptionHeader labelOption = option;
        labelOption.rect = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);
        if (labelOption.rect.isValid())
            style()->drawControl(QStyle::CE_HeaderLabel, &labelOption, painter, this);
        if (option.sortIndicator != QStyleOptionHeader::None) {
            QStyleOptionHeader arrowOption = option;
            arrowOption.rect = style()->subElementRect(QStyle::SE_HeaderArrow, &option, this);
            style()->drawPrimitive(QStyle::PE_IndicatorHeaderArrow, &arrowOption, painter, this);
        }
        painter->restore();
    }

private:
    QPointer<QCheckBox> m_iconEnabled;
    QIcon m_selectedIcon;
};

class StateHeaderAdapter : public viv::LabelHeaderAdapter
{
public:
    StateHeaderAdapter(QCheckBox *gradientEnabled, QCheckBox *iconEnabled)
        : m_gradientEnabled(gradientEnabled), m_iconEnabled(iconEnabled) {}

    QWidget *createSection(viv::WidgetType, QWidget *parent) override
    {
        // Inherited bind/unbind still manage labels, sort indicators and recycled identity.
        return new SelectedIconHeaderSection(m_gradientEnabled.data(), m_iconEnabled.data(), parent);
    }

private:
    QPointer<QCheckBox> m_gradientEnabled;
    QPointer<QCheckBox> m_iconEnabled;
};

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
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::TableHeaderStatesPage)
    {
        setupUi();
        setupExample();
    }

private:
    std::unique_ptr<Ui::TableHeaderStatesPage> ui;

    void setupUi()
    {
        ui->setupUi(this);

        // Keep framework configuration and item data visible in the example.
        ui->view->setObjectName(QStringLiteral("vivItemView"));
    }

    void setupExample()
    {
        // Materialize cells independently so horizontally offscreen cells can also be recycled.
        ui->view->setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
        ui->view->setCellAdapter(new CellAdapter, true);
        // Use a fixed row height to avoid measuring each row.
        ui->view->setUniformItemHeight(40);
        // The header and body share the same column geometry.
        ui->view->setDefaultColumnWidth(150);
        m_model = new QStandardItemModel(120, 8, ui->view);
        for (int column = 0; column < m_model->columnCount(); ++column)
            m_model->setHeaderData(column, Qt::Horizontal, QStringLiteral("列 %1").arg(column + 1));
        for (int row = 0; row < m_model->rowCount(); ++row) {
            for (int column = 0; column < m_model->columnCount(); ++column) {
                m_model->setData(m_model->index(row, column), QStringLiteral("条目 %1 / 列 %2").arg(row + 1).arg(column + 1));
            }
        }
        // The QObject parent owns the model; Qt model signals report data and structure changes.
        ui->view->setModel(m_model);
        // Frozen header clones share the primary adapter and its appearance.
        ui->view->setFrozenColumns({0});
        // Replace only the section factory; retain the built-in label binding and painting.
        // The table owns both headers; each header owns its adapter, also used by pane clones.
        ui->view->horizontalHeader()->setAdapter(new StateHeaderAdapter(ui->gradientHeader, ui->headerIcon), true);
        ui->view->verticalHeader()->setAdapter(new StateHeaderAdapter(ui->gradientHeader, ui->headerIcon), true);
        // Backgrounds are enabled by default; keep content widgets transparent without custom painting.
        ui->view->setVisualStateBackgroundVisible(true);
        ui->view->setVisualStateCornerRadius(8);
        // Enabled by default: row headers round the left edge, row content the right edge.
        // Selected column headers keep all four corners; cell backgrounds stay independent.
        ui->view->setVisualStateCornerRadiusIncludesHeaders(true);
        ui->view->setHoverBackgroundColor(QColor(QStringLiteral("#e4effb")));
        ui->view->setSelectedBackgroundColor(QColor(QStringLiteral("#b8d5f3")));
        // Hover and selected backgrounds both follow the selection behavior.
        ui->view->setSelectionBehavior(viv::VirtualItemView::SelectionBehavior::SelectRows);

        connect(ui->behavior, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            ui->view->selectionModel()->clearSelection();
            ui->view->setSelectionBehavior(value == 0 ? viv::VirtualItemView::SelectionBehavior::SelectRows
                                                   : viv::VirtualItemView::SelectionBehavior::SelectItems);
        });
        connect(ui->radius, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setVisualStateCornerRadius(value);
        });
        connect(ui->headerCorners, &QCheckBox::toggled, this, [this](bool enabled) {
            ui->view->setVisualStateCornerRadiusIncludesHeaders(enabled);
        });
        connect(ui->columnSelection, &QCheckBox::toggled, this, [this](bool enabled) {
            // Header selection is opt-in; use SelectItems above to see each selected cell.
            ui->view->setHorizontalHeaderSelectionEnabled(enabled);
        });
        const auto repaintHeaders = [this](bool) {
            // Repaint existing sections without replacing headers or resetting selection/animation.
            for (auto *section : ui->view->findChildren<viv::HeaderSection *>())
                section->update();
        };
        connect(ui->gradientHeader, &QCheckBox::toggled, this, repaintHeaders);
        connect(ui->headerIcon, &QCheckBox::toggled, this, repaintHeaders);
        connect(ui->headerColor, &QCheckBox::toggled, this, [this](bool enabled) {
            // This works with the default headers too; no custom section or adapter is needed.
            // Invalid QColor restores native panels without changing hover/selection settings.
            const QColor color = enabled ? QColor(QStringLiteral("#fff3d6")) : QColor();
            ui->view->horizontalHeader()->setBackgroundColor(color);
            ui->view->verticalHeader()->setBackgroundColor(color);
        });
        connect(ui->transparentHeader, &QCheckBox::toggled, this, [this](bool transparent) {
            // Hide base fills in both headers and the default corner, keeping state animations.
            ui->view->horizontalHeader()->setBackgroundVisible(!transparent);
            ui->view->verticalHeader()->setBackgroundVisible(!transparent);
        });
        connect(ui->duration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            ui->view->setVisualStateAnimationDuration(value);
        });
        connect(ui->curve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int value) {
            const QEasingCurve::Type curves[] = {QEasingCurve::OutCubic, QEasingCurve::InOutCubic, QEasingCurve::Linear};
            ui->view->setVisualStateAnimationEasingCurve(QEasingCurve(curves[value]));
        });
    }

    QStandardItemModel *m_model = nullptr;
};

QWidget *createExample(QWidget *parent)
{
    return new Example(parent);
}

} // namespace table_header_states
