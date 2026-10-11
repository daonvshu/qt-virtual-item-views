#pragma once

#ifdef VIV_DEMO_USE_ASTER
#include <aster/cache/renderer/imagerenderer.h>
#include <aster/cache/service/imageservice.h>
#include <aster/gui/imagebox.h>
#else
#include "../flow_table_view/thumbnailcache.h"
#endif

#include <QPainter>
#include <QWidget>

#include <functional>
#include <utility>

namespace vivdemo {

class ImageBackend
{
public:
#ifdef VIV_DEMO_USE_ASTER
    ImageBackend()
    {
        aster::cache::ImageServiceConfig config;
        config.renderer = aster::cache::ImageRenderer{};
        config.workerCount = 2;
        config.renderedMemoryBytes = 64 * 1024 * 1024;
        config.encodedMemoryBytes = 32 * 1024 * 1024;
        config.activeMemoryBytes = 64 * 1024 * 1024;
        config.enableDefaultDiskCache = false;
        pipeline = aster::cache::ImageService::createPipeline(config);
    }
    void reset() {}
    QSharedPointer<aster::cache::ImagePipeline> pipeline;
#else
    void reset() { thumbnails.reset(); }
    flowdemo::ThumbnailCache thumbnails;
#endif
};

class ImageContent : public QWidget
{
public:
    using Loaded = std::function<void(const QSize &)>;
    using Failed = std::function<void(const QString &)>;

    ImageContent(ImageBackend *backend, QWidget *parent)
        : QWidget(parent), m_backend(backend)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
#ifdef VIV_DEMO_USE_ASTER
        m_box = new aster::gui::ImageBox(this);
        m_box->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_box->setFocusPolicy(Qt::NoFocus);
        m_box->setPipeline(backend->pipeline);
        m_box->setConfig(aster::gui::ImageBoxConfig()
            .fit(aster::gui::ImageFit::Cover)
            .transition(aster::gui::ImageTransition::CrossFade)
            .scaleAlgorithm(aster::gui::ImageScaleAlgorithm::Lanczos3)
            .offscreenPolicy(aster::gui::OffscreenPolicy::ReleaseImage)
            .build());
        QObject::connect(m_box, &aster::gui::ImageBox::loaded, this, [this]() {
            if (m_path.isEmpty() || m_box->source() != m_path)
                return;
            if (parentWidget())
                parentWidget()->update();
            if (m_reported)
                return;
            const QSize size = m_box->image().size();
            if (!size.isEmpty()) {
                m_reported = true;
                const auto loaded = m_loaded;
                if (loaded)
                    loaded(size);
            }
        });
        QObject::connect(m_box, &aster::gui::ImageBox::loadFailed, this,
            [this](aster::cache::ImageError code, const QString &error) {
                const auto failed = m_failed;
                if (!m_path.isEmpty() && m_box->source() == m_path && failed)
                    failed(error.isEmpty()
                        ? QStringLiteral("图片加载失败（错误码 %1）").arg(int(code)) : error);
            });
#endif
    }

    void setSource(const QString &path, Loaded loaded = {}, Failed failed = {})
    {
        if (path == m_path) {
            m_loaded = std::move(loaded);
            m_failed = std::move(failed);
            return;
        }
        clear();
        m_path = path;
        m_loaded = std::move(loaded);
        m_failed = std::move(failed);
        if (path.isEmpty())
            return;
#ifdef VIV_DEMO_USE_ASTER
        m_box->setGeometry(rect());
        m_box->setSource(path);
#else
        const quint64 binding = m_binding;
        m_backend->thumbnails.request(path, this,
            [this, path, binding](const flowdemo::Thumbnail &thumbnail) {
                if (binding != m_binding || path != m_path)
                    return;
                m_image = thumbnail.image;
                update();
                if (!m_image.isNull()) {
                    const auto loaded = m_loaded;
                    if (loaded)
                        loaded(m_image.size());
                } else if (m_failed) {
                    const auto failed = m_failed;
                    failed(thumbnail.error);
                }
            });
#endif
    }

    void clear()
    {
        m_path.clear();
        m_loaded = {};
        m_failed = {};
#ifdef VIV_DEMO_USE_ASTER
        m_reported = false;
        m_box->setSource(QString());
#else
        ++m_binding;
        m_backend->thumbnails.cancel(this);
        m_image = QPixmap();
#endif
        update();
    }

    bool hasImage() const
    {
#ifdef VIV_DEMO_USE_ASTER
        return !m_box->image().isNull();
#else
        return !m_image.isNull();
#endif
    }

protected:
#ifdef VIV_DEMO_USE_ASTER
    void resizeEvent(QResizeEvent *) override { m_box->setGeometry(rect()); }
#else
    void paintEvent(QPaintEvent *) override
    {
        if (m_image.isNull() || rect().isEmpty())
            return;
        const QSize size = m_image.size().scaled(rect().size(), Qt::KeepAspectRatioByExpanding);
        const QRect target((width() - size.width()) / 2, (height() - size.height()) / 2,
                           size.width(), size.height());
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(target, m_image);
    }
#endif

private:
    ImageBackend *m_backend;
    QString m_path;
    Loaded m_loaded;
    Failed m_failed;
#ifdef VIV_DEMO_USE_ASTER
    aster::gui::ImageBox *m_box = nullptr;
    bool m_reported = false;
#else
    QPixmap m_image;
    quint64 m_binding = 0;
#endif
};

} // namespace vivdemo
