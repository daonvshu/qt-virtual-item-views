#pragma once

#include <virtualitemviews/treevisibilityindex.h>

#include <QAbstractItemModel>
#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QEvent>
#include <QHash>
#include <QPainter>
#include <QPointer>
#include <QRegion>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QVector>
#include <QWidget>
#include <functional>
#include <utility>

namespace viv {

// Keeps animation pixels separate from the committed tree geometry and widgets.
class TreeExpansionTransition : public QWidget
{
public:
    struct Row {
        QPersistentModelIndex index;
        QRect rect;
        int bodyHeight = 0;
    };
    struct Snapshot {
        QPixmap pixels;
        QVector<Row> rows;
        QHash<QPersistentModelIndex, QRect> existingRects;
    };

    explicit TreeExpansionTransition(QAbstractScrollArea *view,
                                     std::function<QVector<Row>()> rows,
                                     std::function<QRect(const QModelIndex &)> geometry)
        : QWidget(view), m_view(view), m_rows(std::move(rows)), m_geometry(std::move(geometry))
    {
        setObjectName(QStringLiteral("vivTreeExpansionTransition"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
        view->installEventFilter(this);
        view->viewport()->installEventFilter(this);
        connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this,
                [this]() { stop(); });
        connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, this,
                [this]() { stop(); });
        m_animation.setStartValue(1.0);
        m_animation.setEndValue(0.0);
        m_animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    m_opacity = value.toReal();
                    update();
                });
        connect(&m_animation, &QVariantAnimation::finished, this,
                [this]() { stop(); });
    }

    void watchModel(QAbstractItemModel *model)
    {
        if (m_model)
            disconnect(m_model, nullptr, this, nullptr);
        m_model = model;
        stop();
        if (!model)
            return;
        const auto cancel = [this]() { stop(); };
        connect(model, &QAbstractItemModel::modelAboutToBeReset, this, cancel);
        connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, cancel);
        connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, cancel);
        connect(model, &QAbstractItemModel::rowsAboutToBeMoved, this, cancel);
        connect(model, &QAbstractItemModel::columnsAboutToBeInserted, this, cancel);
        connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, this, cancel);
        connect(model, &QAbstractItemModel::columnsAboutToBeMoved, this, cancel);
        connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this, cancel);
        connect(model, &QAbstractItemModel::dataChanged, this, cancel);
    }

    Snapshot capture(const TreeVisibilityIndex *visibility = nullptr,
                     const QModelIndex &collapsingNode = QModelIndex())
    {
        stop();
        if (!m_enabled || m_duration <= 0 || !m_view->isVisible())
            return {};
        const auto rows = m_rows();
        Snapshot snapshot;
        snapshot.rows = rows;
        for (const auto &row : rows)
            snapshot.existingRects.insert(row.index, row.rect);
        if (visibility) {
            // Retain coordinates, not pixels/widgets, for rows a collapse can reveal.
            // At most one viewport of one-pixel rows can enter the final frame.
            qsizetype first = 0;
            if (collapsingNode.isValid()) {
                first = visibility->visibleRowForIndex(collapsingNode);
                if (first >= 0) {
                    const int depth = visibility->depth(collapsingNode);
                    ++first;
                    while (first < visibility->visibleRowCount()
                           && visibility->depth(visibility->indexAtVisibleRow(first)) > depth)
                        ++first;
                }
            }
            const int limit = m_view->viewport()->height() + 1;
            for (int count = 0; first >= 0 && first < visibility->visibleRowCount()
                                && count < limit; ++count) {
                const QModelIndex index = visibility->indexAtVisibleRow(first);
                snapshot.existingRects.insert(QPersistentModelIndex(index), m_geometry(index));
                if (collapsingNode.isValid()) {
                    ++first;
                } else {
                    // collapseAll retains only the children of the display root.
                    const QModelIndex next = index.model()->index(index.row() + 1, 0, index.parent());
                    first = visibility->visibleRowForIndex(next);
                }
            }
        }
        snapshot.pixels = m_view->grab();
        return snapshot;
    }

    void start(const Snapshot &before)
    {
        if (before.pixels.isNull() || !m_enabled || m_duration <= 0 || !m_view->isVisible())
            return;
        const QPointer<TreeExpansionTransition> self(this);
        const quint64 revision = m_revision;
        const auto afterRows = m_rows();
        const QPixmap after = m_view->grab();
        if (!self || m_revision != revision)
            return;
        m_before = before.pixels;
        m_after = after;
        const QRect viewport = m_view->viewport()->geometry();
        m_body = QRect(m_view->frameWidth(), viewport.top(),
                       viewport.right() - m_view->frameWidth() + 1, viewport.height());
        m_strips.clear();
        const auto &oldRects = before.existingRects;
        QHash<QPersistentModelIndex, QRect> newRects;
        QHash<QPersistentModelIndex, int> oldBodyHeights;
        for (const auto &row : before.rows)
            oldBodyHeights.insert(row.index, row.bodyHeight);
        for (const auto &row : afterRows)
            newRects.insert(row.index, row.rect);
        // Missing descendants grow from their nearest previously visible ancestor.
        // Rows leaving the viewport keep their final off-screen destination.
        const auto collapsedRect = [this](const Row &row,
                                         const QHash<QPersistentModelIndex, QRect> &rects) {
            for (QModelIndex parent = row.index.parent(); parent.isValid(); parent = parent.parent()) {
                const auto it = rects.constFind(QPersistentModelIndex(parent));
                if (it != rects.cend())
                    return QRect(0, it->bottom() + 1, width(), 0);
            }
            return QRect(0, m_body.bottom() + 1, width(), 0);
        };
        for (const auto &row : afterRows) {
            const auto it = oldRects.constFind(row.index);
            // A final-frame row may already be clipped at the bottom. Keep the
            // more complete source so its pixels leave with the animated row.
            const bool useOld = it != oldRects.cend()
                && it->intersected(m_body).height() > row.rect.intersected(m_body).height();
            m_strips.append({it == oldRects.cend() ? collapsedRect(row, oldRects) : *it,
                             row.rect, useOld ? *it : row.rect, useOld, it == oldRects.cend(),
                             useOld ? oldBodyHeights.value(row.index, row.bodyHeight) : row.bodyHeight});
        }
        for (const auto &row : before.rows) {
            if (newRects.contains(row.index))
                continue;
            QRect destination = m_geometry(row.index);
            const bool removed = destination.isEmpty();
            if (removed)
                destination = collapsedRect(row, newRects);
            m_strips.append({row.rect, destination, row.rect, true, removed, row.bodyHeight});
        }
        m_opacity = 1;
        setGeometry(m_view->rect());
        show();
        raise();
        QCoreApplication::instance()->installEventFilter(this);
        m_animation.setDuration(m_duration);
        m_animation.start();
    }

    void stop()
    {
        ++m_revision;
        if (auto *app = QCoreApplication::instance())
            app->removeEventFilter(this);
        m_animation.stop();
        hide();
        m_before = QPixmap();
        m_after = QPixmap();
        m_strips.clear();
    }

    void setEnabled(bool enabled) { m_enabled = enabled; if (!enabled) stop(); }
    bool isEnabled() const { return m_enabled; }
    void setDuration(int ms) { m_duration = qMax(0, ms); stop(); }
    int duration() const { return m_duration; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || widget == this
            || (widget != m_view && !m_view->isAncestorOf(widget)))
            return false;
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Hide:
        case QEvent::Wheel:
        case QEvent::MouseButtonPress:
        case QEvent::KeyPress:
            stop();
            break;
        default:
            break;
        }
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (m_strips.isEmpty()) {
            painter.setOpacity(m_opacity);
            painter.drawPixmap(0, 0, m_before);
            return;
        }
        painter.drawPixmap(0, 0, m_after);
        painter.save();
        painter.setClipRect(m_body);
        painter.fillRect(m_body, m_view->viewport()->palette().brush(QPalette::Base));
        const qreal progress = 1 - m_opacity;
        for (const auto &strip : m_strips) {
            const qreal top = strip.from.top() + (strip.to.top() - strip.from.top()) * progress;
            const qreal height = strip.from.height()
                + (strip.to.height() - strip.from.height()) * progress;
            if (height <= 0)
                continue;
            painter.setOpacity(strip.fade ? (strip.old ? m_opacity : progress) : 1);
            const QPixmap &pixels = strip.old ? m_before : m_after;
            const qreal dpr = pixels.devicePixelRatio();
            // Reveal/clip row content at its own height; only the following gap stretches.
            const qreal bodyHeight = qMin(height, qreal(strip.bodyHeight));
            if (bodyHeight > 0) {
                painter.drawPixmap(QRectF(0, top, width(), bodyHeight), pixels,
                    QRectF(strip.source.x() * dpr, strip.source.y() * dpr,
                           strip.source.width() * dpr, bodyHeight * dpr));
            }
            const qreal gapHeight = height - bodyHeight;
            const int sourceGap = strip.source.height() - strip.bodyHeight;
            if (gapHeight > 0 && sourceGap > 0) {
                painter.drawPixmap(QRectF(0, top + bodyHeight, width(), gapHeight), pixels,
                    QRectF(strip.source.x() * dpr, (strip.source.y() + strip.bodyHeight) * dpr,
                           strip.source.width() * dpr, sourceGap * dpr));
            }
        }
        painter.restore();
        painter.setClipRegion(QRegion(rect()).subtracted(QRegion(m_body)));
        painter.setOpacity(m_opacity);
        painter.drawPixmap(0, 0, m_before);
    }

private:
    struct Strip {
        QRect from;
        QRect to;
        QRect source;
        bool old;
        bool fade;
        int bodyHeight;
    };
    QAbstractScrollArea *m_view;
    std::function<QVector<Row>()> m_rows;
    std::function<QRect(const QModelIndex &)> m_geometry;
    QPointer<QAbstractItemModel> m_model;
    QVariantAnimation m_animation;
    QPixmap m_before;
    QPixmap m_after;
    QRect m_body;
    QVector<Strip> m_strips;
    qreal m_opacity = 0;
    bool m_enabled = false;
    int m_duration = 300;
    quint64 m_revision = 0;
};

} // namespace viv
