#include "example.h"
#include "ui_images.h"
#include "cards.h"
#include "imagecatalog.h"
#include <QFileDialog>
#include <QFileInfo>
#include <memory>

namespace masonry_images {

class Example : public QWidget
{
public:
    explicit Example(QWidget *parent) : QWidget(parent), ui(new Ui::MasonryImagesPage)
    {
        ui->setupUi(this);
        setupExample();
    }
    ~Example() override
    {
        catalog.cancel();
        // Unbind all cards while their backend still exists.
        delete ui->view;
    }
private:
    void setupExample()
    {
        model = new QStandardItemModel(0, 1, ui->view);
        ui->view->setAdapter(new ImageAdapter(&backend), true);
        // The manifest stores original, orientation-corrected sizes before decoding.
        ui->view->setItemHeightMode(viv::VirtualMasonryView::ItemHeightMode::AdapterEstimate);
        ui->view->setCellWidthRange(120, 220);
        ui->view->setRowSpacing(12);
        ui->view->setColumnSpacing(12);
        ui->view->setItemBackgroundColor(QColor(QStringLiteral("#eef3f8")));
        ui->view->setStateBackgroundCornerRadius(8);
        ui->view->setHorizontalGridLinesVisible(false);
        ui->view->setVerticalGridLinesVisible(false);
        ui->view->setModel(model);
        connect(ui->load, &QPushButton::clicked, this, [this]() {
            const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("选择图片文件夹"));
            if (folder.isEmpty()) return;
            catalog.loadFolder(folder, [this](const QVector<masonry_images_catalog::ImageEntry> &entries) {
                // Removing rows cancels old bindings before resetting the cache.
                model->removeRows(0, model->rowCount());
                backend.reset();
                QList<QStandardItem *> items;
                for (const auto &entry : entries) {
                    auto *item = new QStandardItem(QFileInfo(entry.path).fileName());
                    item->setData(entry.path, Qt::UserRole);
                    item->setData(entry.size, Qt::UserRole + 1);
                    items.append(item);
                }
                model->invisibleRootItem()->appendRows(items);
                ui->status->setText(QStringLiteral("Loaded %1 image paths; scroll to observe widget reuse.").arg(entries.size()));
            });
            ui->status->setText(QStringLiteral("正在后台读取尺寸清单；重复加载会取消上次扫描。"));
        });
        connect(ui->clear, &QPushButton::clicked, this, [this]() {
            catalog.cancel();
            model->removeRows(0, model->rowCount());
            backend.reset();
            ui->status->setText(QStringLiteral("已清空。"));
        });
        connect(ui->lines, &QCheckBox::toggled, this, [this](bool on) {
            ui->view->setHorizontalGridLinesVisible(on);
            ui->view->setVerticalGridLinesVisible(on);
        });
        ui->status->setText(QStringLiteral("尚未加载图片。可选 aster 后端由 CMake 检测；未检测到时使用 Qt 后台缩略图。"));
    }
    std::unique_ptr<Ui::MasonryImagesPage> ui;
    masonry_images_backend::ImageBackend backend;
    masonry_images_catalog::ImageCatalog catalog;
    QStandardItemModel *model = nullptr;
};

QWidget *createExample(QWidget *parent) { return new Example(parent); }

} // namespace masonry_images
