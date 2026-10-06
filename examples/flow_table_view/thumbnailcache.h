#pragma once

#include <QCache>
#include <QHash>
#include <QImageReader>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QQueue>
#include <QRunnable>
#include <QSet>
#include <QThreadPool>
#include <QVector>

#include <functional>
#include <utility>

namespace flowdemo {

struct Thumbnail
{
    QPixmap image;
    QString error;
};

class ThumbnailTask : public QRunnable
{
public:
    explicit ThumbnailTask(std::function<void()> work) : m_work(std::move(work)) {}
    void run() override { m_work(); }
private:
    std::function<void()> m_work;
};

class ThumbnailCache : public QObject
{
public:
    using Callback = std::function<void(const Thumbnail &)>;

    explicit ThumbnailCache(QObject *parent = nullptr) : QObject(parent), m_cache(64 * 1024)
    {
        m_pool.setMaxThreadCount(2);
    }
    ~ThumbnailCache() override { m_pool.waitForDone(); }

    void request(const QString &path, QObject *receiver, Callback callback)
    {
        if (const auto *thumbnail = m_cache.object(path)) {
            callback(*thumbnail);
            return;
        }
        const bool queued = m_pending.contains(path);
        m_pending[path].append({receiver, std::move(callback)});
        if (!queued && !m_activePaths.contains(path))
            m_queue.enqueue(path);
        pump();
    }
    void cancel(QObject *receiver)
    {
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            auto &waiters = it.value();
            for (int i = waiters.size() - 1; i >= 0; --i) {
                if (!waiters.at(i).receiver || waiters.at(i).receiver == receiver)
                    waiters.removeAt(i);
            }
            if (waiters.isEmpty())
                it = m_pending.erase(it);
            else
                ++it;
        }
    }
    void reset()
    {
        ++m_generation;
        m_pending.clear();
        m_queue.clear();
        m_activePaths.clear();
        m_cache.clear();
    }

private:
    struct Waiter
    {
        QPointer<QObject> receiver;
        Callback callback;
    };

    void pump()
    {
        while (m_active < 2 && !m_queue.isEmpty()) {
            const QString path = m_queue.dequeue();
            if (!m_pending.contains(path) || m_activePaths.contains(path))
                continue;
            ++m_active;
            m_activePaths.insert(path);
            const quint64 generation = m_generation;
            m_pool.start(new ThumbnailTask([this, path, generation]() {
                QImageReader reader(path);
                reader.setAutoTransform(true);
                const QSize size = reader.size();
                if (size.isValid() && (size.width() > 512 || size.height() > 512))
                    reader.setScaledSize(size.scaled(512, 512, Qt::KeepAspectRatio));
                QImage image = reader.read();
                if (!image.isNull() && (image.width() > 512 || image.height() > 512))
                    image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                const QString error = image.isNull() ? reader.errorString() : QString();
                QMetaObject::invokeMethod(this, [this, path, generation, image, error]() {
                    --m_active;
                    if (generation == m_generation) {
                        m_activePaths.remove(path);
                        const Thumbnail thumbnail{QPixmap::fromImage(image), error};
                        const int cost = qMax(1, int((qint64(thumbnail.image.width())
                            * thumbnail.image.height() * 4 + 1023) / 1024));
                        m_cache.insert(path, new Thumbnail(thumbnail), cost);
                        const auto waiters = m_pending.take(path);
                        for (const auto &waiter : waiters) {
                            if (waiter.receiver)
                                waiter.callback(thumbnail);
                        }
                    }
                    pump();
                }, Qt::QueuedConnection);
            }));
        }
    }

    QThreadPool m_pool;
    QCache<QString, Thumbnail> m_cache;
    QHash<QString, QVector<Waiter>> m_pending;
    QQueue<QString> m_queue;
    QSet<QString> m_activePaths;
    quint64 m_generation = 0;
    int m_active = 0;
};

} // namespace flowdemo
