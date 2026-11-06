#pragma once

#include "imagebackend.h"
#include <virtualitemviews/widgetadapter.h>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace masonry_images {

class ImageCard : public QWidget
{
public:
    ImageCard(masonry_images_backend::ImageBackend *backend, QWidget *parent) : QWidget(parent)
    {
        setupUi(backend);
    }
    void bind(const QModelIndex &index)
    {
        title->setText(index.data().toString());
        const QString path = index.data(Qt::UserRole).toString();
        setToolTip(path);
        // Decoding does not replace the manifest's original dimensions with thumbnail dimensions.
        content->setSource(path, {}, [this](const QString &error) { setToolTip(error); });
    }
    void unbind()
    {
        // Clear the asynchronous binding before recycling the card.
        content->clear();
        title->clear();
        setToolTip(QString());
    }
private:
    void setupUi(masonry_images_backend::ImageBackend *backend)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        content = new masonry_images_backend::ImageContent(backend, this);
        title = new QLabel(this);
        title->setAlignment(Qt::AlignCenter);
        title->setMinimumWidth(0);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(content, 1);
        layout->addWidget(title);
    }
    masonry_images_backend::ImageContent *content = nullptr;
    QLabel *title = nullptr;
};

class ImageAdapter : public viv::WidgetAdapter
{
public:
    explicit ImageAdapter(masonry_images_backend::ImageBackend *backend) : m_backend(backend) {}
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override { return new ImageCard(m_backend, parent); }
    void bindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &index) override
    {
        static_cast<ImageCard *>(widget)->bind(index);
    }
    void unbindWidget(viv::WidgetType, QWidget *widget, const QModelIndex &) override
    {
        static_cast<ImageCard *>(widget)->unbind();
    }
    QSize estimatedSize(viv::WidgetType, const QModelIndex &index) const override
    {
        return index.data(Qt::UserRole + 1).toSize();
    }
private:
    masonry_images_backend::ImageBackend *m_backend;
};

} // namespace masonry_images
