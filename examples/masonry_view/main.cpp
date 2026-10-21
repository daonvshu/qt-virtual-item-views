#include <virtualitemviews/virtualmasonryview.h>
#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/accessibility.h>

#include "cards.h"

#include <QAbstractListModel>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMainWindow>
#include <QMimeData>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>

namespace {

using masonrydemo::MasonryModel;
using masonrydemo::MasonryAdapter;

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    viv::installAccessibilityFactory();
    MasonryModel model(1000);
    vivdemo::ImageBackend thumbnails;
    vivdemo::ImageCatalog catalog;
    MasonryAdapter adapter(&model, &thumbnails);
    QMainWindow window;
    viv::VirtualMasonryView view;
    view.setAccessibleName(QStringLiteral("瀑布流视图"));
    view.setAdapter(&adapter);
    view.setModel(&model);
    view.setCellWidthRange(100, 200);
    view.setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
    view.setColumnSpacing(12);
    view.setRowSpacing(12);
    view.setSelectionMode(viv::VirtualItemView::SelectionMode::ExtendedSelection);
    view.setDragDropActions(Qt::MoveAction);
    view.setDefaultDropAction(Qt::MoveAction);

    window.setCentralWidget(&view);
    window.setWindowTitle(QStringLiteral("VirtualMasonryView — 最短列瀑布流"));
    auto *toolbar = window.addToolBar(QStringLiteral("瀑布流"));
    auto *loadImages = toolbar->addAction(QStringLiteral("加载图片文件夹"));
    QObject::connect(loadImages, &QAction::triggered, &window,
        [&window, &model, &thumbnails, &catalog]() {
            const QString folder = QFileDialog::getExistingDirectory(
                &window, QStringLiteral("选择图片文件夹"));
            if (folder.isEmpty())
                return;
            window.statusBar()->showMessage(QStringLiteral("正在读取图片尺寸：%1").arg(folder));
            catalog.loadFolder(folder, [&window, &model, &thumbnails, folder](
                const QVector<vivdemo::ImageEntry> &entries) {
                thumbnails.reset();
                model.setImages(entries);
                window.statusBar()->showMessage(
                    QStringLiteral("已加载 %1 张图片：%2").arg(entries.size()).arg(folder));
            });
        });
    auto *resetDemo = toolbar->addAction(QStringLiteral("恢复示例"));
    QObject::connect(resetDemo, &QAction::triggered, &window,
        [&model, &thumbnails, &view, &catalog, &window]() {
            catalog.cancel();
            thumbnails.reset();
            model.setSyntheticItems(1000);
            view.setCurrentIndex(model.index(0, 0));
            view.scrollTo(model.index(0, 0), viv::VirtualItemView::PositionAtTop);
            view.setFocus();
            window.statusBar()->showMessage(QStringLiteral("已恢复示例"));
        });

    auto *dragEnabled = new QCheckBox(QStringLiteral("允许拖放"), &window);
    dragEnabled->setChecked(true);
    toolbar->addWidget(dragEnabled);
    QObject::connect(dragEnabled, &QCheckBox::toggled, &view,
        &viv::VirtualItemView::setDragEnabled);
    view.setDragEnabled(true);
    auto *dropIndicator = new QCheckBox(QStringLiteral("拖放指示器"), &window);
    dropIndicator->setChecked(true);
    toolbar->addWidget(dropIndicator);
    QObject::connect(dropIndicator, &QCheckBox::toggled, &view,
        &viv::VirtualItemView::setDropIndicatorShown);
    view.setDropIndicatorShown(true);

    const auto spin = [&window, &toolbar](const QString &label, int value,
                                         int minimum, int maximum) {
        auto *caption = new QLabel(label, &window);
        toolbar->addWidget(caption);
        auto *control = new QSpinBox(&window);
        control->setAccessibleName(label);
        caption->setBuddy(control);
        control->setRange(minimum, maximum);
        control->setValue(value);
        toolbar->addWidget(control);
        return control;
    };
    const auto toggle = [&window, &toolbar](const QString &label, bool value) {
        auto *control = new QCheckBox(label, &window);
        control->setChecked(value);
        toolbar->addWidget(control);
        return control;
    };
    window.addToolBarBreak();
    toolbar = window.addToolBar(QStringLiteral("布局与间距"));
    toolbar->addWidget(new QLabel(QStringLiteral("列数"), &window));
    auto *columns = new QSpinBox(&window);
    columns->setRange(0, 20);
    columns->setValue(0);
    columns->setSpecialValueText(QStringLiteral("自动"));
    columns->setAccessibleName(QStringLiteral("列数，零表示自动"));
    toolbar->addWidget(columns);
    QObject::connect(columns, QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setColumnCount);

    toolbar->addWidget(new QLabel(QStringLiteral("高度模式"), &window));
    auto *mode = new QComboBox(&window);
    mode->addItems({QStringLiteral("固定"), QStringLiteral("宽高比"), QStringLiteral("估算")});
    mode->setCurrentIndex(2);
    mode->setAccessibleName(QStringLiteral("高度模式"));
    toolbar->addWidget(mode);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), &view,
        [&view](int index) {
            view.setItemHeightMode(static_cast<viv::VirtualMasonryView::ItemHeightMode>(index));
        });

    toolbar->addWidget(new QLabel(QStringLiteral("列间距"), &window));
    auto *columnSpacing = new QSpinBox(&window);
    columnSpacing->setRange(0, 100);
    columnSpacing->setValue(view.columnSpacing());
    columnSpacing->setAccessibleName(QStringLiteral("列间距"));
    toolbar->addWidget(columnSpacing);
    QObject::connect(columnSpacing, QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setColumnSpacing);

    toolbar->addWidget(new QLabel(QStringLiteral("行间距"), &window));
    auto *rowSpacing = new QSpinBox(&window);
    rowSpacing->setRange(0, 100);
    rowSpacing->setValue(view.rowSpacing());
    rowSpacing->setAccessibleName(QStringLiteral("行间距"));
    toolbar->addWidget(rowSpacing);
    QObject::connect(rowSpacing, QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setRowSpacing);

    window.addToolBarBreak();
    toolbar = window.addToolBar(QStringLiteral("分割线"));
    auto *verticalLines = new QCheckBox(QStringLiteral("竖分割线"), &window);
    verticalLines->setChecked(view.verticalGridLinesVisible());
    toolbar->addWidget(verticalLines);
    QObject::connect(verticalLines, &QCheckBox::toggled, &view,
        &viv::VirtualMasonryView::setVerticalGridLinesVisible);
    auto *horizontalLines = new QCheckBox(QStringLiteral("横分割线"), &window);
    horizontalLines->setChecked(view.horizontalGridLinesVisible());
    toolbar->addWidget(horizontalLines);
    QObject::connect(horizontalLines, &QCheckBox::toggled, &view,
        &viv::VirtualMasonryView::setHorizontalGridLinesVisible);
    QObject::connect(toggle(QStringLiteral("横线穿列隙"),
                           view.horizontalSpacingLineThroughColumnSpacing()),
        &QCheckBox::toggled, &view,
        &viv::VirtualMasonryView::setHorizontalSpacingLineThroughColumnSpacing);
    QObject::connect(toggle(QStringLiteral("竖线穿行隙"),
                           view.verticalSpacingLineThroughRowSpacing()),
        &QCheckBox::toggled, &view,
        &viv::VirtualMasonryView::setVerticalSpacingLineThroughRowSpacing);
    QObject::connect(spin(QStringLiteral("横线 px"), view.horizontalGridLineWidth(), 0, 8),
        QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setHorizontalGridLineWidth);
    QObject::connect(spin(QStringLiteral("竖线 px"), view.verticalGridLineWidth(), 0, 8),
        QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setVerticalGridLineWidth);

    window.addToolBarBreak();
    toolbar = window.addToolBar(QStringLiteral("高度与动画"));
    QObject::connect(spin(QStringLiteral("固定高度"), view.fixedItemHeight(), 1, 2000),
        QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setFixedItemHeight);
    auto *ratioCaption = new QLabel(QStringLiteral("宽 / 高"), &window);
    toolbar->addWidget(ratioCaption);
    auto *ratio = new QDoubleSpinBox(&window);
    ratio->setAccessibleName(QStringLiteral("宽高比"));
    ratioCaption->setBuddy(ratio);
    ratio->setRange(0.1, 10);
    ratio->setSingleStep(0.1);
    ratio->setValue(view.itemAspectRatio());
    toolbar->addWidget(ratio);
    QObject::connect(ratio, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setItemAspectRatio);
    auto *heightAnimation = new QCheckBox(QStringLiteral("高度动画"), &window);
    heightAnimation->setChecked(view.itemHeightAnimationEnabled());
    toolbar->addWidget(heightAnimation);
    QObject::connect(heightAnimation, &QCheckBox::toggled, &view,
        &viv::VirtualMasonryView::setItemHeightAnimationEnabled);
    toolbar->addWidget(new QLabel(QStringLiteral("动画 ms"), &window));
    auto *animationDuration = new QSpinBox(&window);
    animationDuration->setRange(0, 2000);
    animationDuration->setValue(view.itemHeightAnimationDuration());
    animationDuration->setAccessibleName(QStringLiteral("高度动画时长，毫秒"));
    toolbar->addWidget(animationDuration);
    QObject::connect(animationDuration, QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setItemHeightAnimationDuration);
    auto *itemHeight = spin(QStringLiteral("当前项高度"), 240, 1, 2000);
    auto *applyHeight = toolbar->addAction(QStringLiteral("应用高度"));
    QObject::connect(applyHeight, &QAction::triggered, &window,
        [&window, &view, itemHeight]() {
            const QModelIndex current = view.currentIndex();
            if (!current.isValid()) {
                window.statusBar()->showMessage(QStringLiteral("先单击一个项目，再应用高度。"), 4000);
                return;
            }
            view.scrollTo(current);
            view.setItemHeight(current, itemHeight->value());
            view.setFocus();
        });

    window.addToolBarBreak();
    toolbar = window.addToolBar(QStringLiteral("状态背景"));
    QObject::connect(toggle(QStringLiteral("状态背景"), view.visualStateBackgroundVisible()),
        &QCheckBox::toggled, &view, &viv::VirtualMasonryView::setVisualStateBackgroundVisible);
    QObject::connect(spin(QStringLiteral("圆角"), view.stateBackgroundCornerRadius(), 0, 100),
        QOverload<int>::of(&QSpinBox::valueChanged), &view,
        &viv::VirtualMasonryView::setStateBackgroundCornerRadius);

    auto *status = new QLabel(&window);
    window.statusBar()->addPermanentWidget(status);
    const auto updateStatus = [&view, &model, status]() {
        const auto stats = view.stats();
        const QModelIndex current = view.currentIndex();
        status->setText(QStringLiteral("列 %1 | 项目 %2 | 已物化 %3 | 当前项 %4 | 实际高度 %5")
            .arg(view.columnCount()).arg(model.rowCount()).arg(stats.materializedItems)
            .arg(current.isValid() ? QString::number(current.row()) : QStringLiteral("无"))
            .arg(view.visualRect(current).height()));
    };
    QObject::connect(&view, &viv::VirtualItemView::virtualizationUpdated, &window,
        updateStatus);
    QObject::connect(&view, &viv::VirtualMasonryView::masonryLayoutChanged, &window,
        updateStatus);
    QObject::connect(view.selectionModel(), &QItemSelectionModel::currentChanged, &window,
        updateStatus);
    updateStatus();

    window.resize(900, 700);
    window.show();
    view.setCurrentIndex(model.index(0, 0));
    view.setFocus();
    return app.exec();
}
