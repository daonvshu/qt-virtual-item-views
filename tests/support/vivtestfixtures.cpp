#include "vivtestfixtures.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>

namespace vivtest {

// ---------------------------------------------------------------------------
// StringListModel
// ---------------------------------------------------------------------------

StringListModel::StringListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

StringListModel::StringListModel(const QStringList &rows, QObject *parent)
    : QAbstractListModel(parent)
    , m_rows(rows)
{
}

int StringListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant StringListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    switch (role) {
    case Qt::DisplayRole:
        return m_rows.at(index.row());
    case HeightRole:
        return m_heights.value(index.row(), 0);
    default:
        return QVariant();
    }
}

bool StringListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return false;
    if (role == HeightRole) {
        m_heights[index.row()] = value.toInt();
        emit dataChanged(index, index, {HeightRole});
        return true;
    }
    if (role != Qt::EditRole && role != Qt::DisplayRole)
        return false;

    m_rows[index.row()] = value.toString();
    emit dataChanged(index, index, {Qt::DisplayRole});
    return true;
}

void StringListModel::appendRow(const QString &text)
{
    const int row = int(m_rows.size());
    beginInsertRows(QModelIndex(), row, row);
    m_rows.append(text);
    endInsertRows();
}

void StringListModel::insertRowsAt(int first, const QStringList &texts)
{
    if (texts.isEmpty())
        return;
    const int clamped = qBound(0, first, int(m_rows.size()));
    beginInsertRows(QModelIndex(), clamped, clamped + int(texts.size()) - 1);
    for (int i = 0; i < texts.size(); ++i)
        m_rows.insert(clamped + i, texts.at(i));
    endInsertRows();
}

bool StringListModel::removeRowsAt(int first, int count)
{
    if (count <= 0 || first < 0 || first + count > m_rows.size())
        return false;
    beginRemoveRows(QModelIndex(), first, first + count - 1);
    for (int i = 0; i < count; ++i)
        m_rows.removeAt(first);
    endRemoveRows();

    // Row keyed metadata has to follow the removal.
    QHash<int, int> shifted;
    for (auto it = m_heights.constBegin(); it != m_heights.constEnd(); ++it) {
        if (it.key() < first)
            shifted.insert(it.key(), it.value());
        else if (it.key() >= first + count)
            shifted.insert(it.key() - count, it.value());
    }
    m_heights = shifted;
    return true;
}

bool StringListModel::moveRow(int from, int to)
{
    if (from < 0 || from >= m_rows.size() || to < 0 || to > m_rows.size())
        return false;
    if (to == from || to == from + 1)
        return false;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), to))
        return false;
    m_rows.move(from, to > from ? to - 1 : to);
    endMoveRows();
    return true;
}

void StringListModel::setRowText(int row, const QString &text)
{
    if (row < 0 || row >= m_rows.size())
        return;
    m_rows[row] = text;
    const QModelIndex index = this->index(row, 0);
    emit dataChanged(index, index, {Qt::DisplayRole});
}

void StringListModel::setRowHeight(int row, int height)
{
    if (row < 0 || row >= m_rows.size())
        return;
    m_heights[row] = height;
    const QModelIndex index = this->index(row, 0);
    emit dataChanged(index, index, {HeightRole});
}

void StringListModel::replaceAll(const QStringList &rows)
{
    beginResetModel();
    m_rows = rows;
    m_heights.clear();
    endResetModel();
}

void StringListModel::reverseKeepingPersistentIndexes()
{
    const QModelIndexList oldIndexes = persistentIndexList();
    emit layoutAboutToBeChanged();

    std::reverse(m_rows.begin(), m_rows.end());

    QModelIndexList newIndexes;
    newIndexes.reserve(oldIndexes.size());
    for (const QModelIndex &old : oldIndexes)
        newIndexes.append(index(int(m_rows.size()) - 1 - old.row(), 0));
    changePersistentIndexList(oldIndexes, newIndexes);

    emit layoutChanged();
}

// ---------------------------------------------------------------------------
// TestRowWidget
// ---------------------------------------------------------------------------

NumericListModel::NumericListModel(int count, QObject *parent)
    : QAbstractListModel(parent)
    , m_count(qMax(0, count))
{
}

int NumericListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_count;
}

QVariant NumericListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_count)
        return QVariant();
    if (role == Qt::DisplayRole)
        return textForRow(index.row());
    return QVariant();
}

QString NumericListModel::textForRow(int row)
{
    return QStringLiteral("Item %1").arg(row);
}

void NumericListModel::setCount(int count)
{
    beginResetModel();
    m_count = qMax(0, count);
    endResetModel();
}

void NumericListModel::appendRows(int count)
{
    if (count <= 0)
        return;
    beginInsertRows(QModelIndex(), m_count, m_count + count - 1);
    m_count += count;
    endInsertRows();
}

bool NumericListModel::removeRowsAt(int first, int count)
{
    if (count <= 0 || first < 0 || first + count > m_count)
        return false;
    beginRemoveRows(QModelIndex(), first, first + count - 1);
    m_count -= count;
    endRemoveRows();
    return true;
}

bool NumericListModel::moveRow(int from, int to)
{
    if (from < 0 || from >= m_count || to < 0 || to > m_count || to == from || to == from + 1)
        return false;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), to))
        return false;
    endMoveRows();
    return true;
}

// ---------------------------------------------------------------------------
// TestRowWidget
// ---------------------------------------------------------------------------

TestRowWidget::TestRowWidget(QWidget *parent, int preferredHeight)
    : QWidget(parent)
    , m_preferredHeight(preferredHeight)
{
    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("rowLabel"));
    m_label->setGeometry(0, 0, 200, 20);
}

QString TestRowWidget::text() const
{
    return m_label->text();
}

void TestRowWidget::setText(const QString &text)
{
    m_label->setText(text);
}

QSize TestRowWidget::sizeHint() const
{
    if (m_preferredHeight > 0)
        return QSize(200, m_preferredHeight);
    return QSize(200, 24);
}

void TestRowWidget::setPreferredHeight(int height)
{
    m_preferredHeight = qMax(0, height);
    updateGeometry();
}

// ---------------------------------------------------------------------------
// TestAdapter
// ---------------------------------------------------------------------------

TestAdapter::TestAdapter(int estimatedHeight)
    : m_estimatedHeight(qMax(1, estimatedHeight))
{
}

viv::WidgetType TestAdapter::widgetType(const QModelIndex &index) const
{
    if (m_typeCount <= 1)
        return viv::kDefaultWidgetType;
    return index.row() % m_typeCount;
}

QWidget *TestAdapter::createWidget(viv::WidgetType type, QWidget *parent)
{
    ++m_createdCount;
    QWidget *widget = new TestRowWidget(parent);
    widget->setObjectName(QStringLiteral("rowWidget_%1").arg(type));
    return widget;
}

void TestAdapter::bindWidget(QWidget *widget, const QModelIndex &index)
{
    auto *row = static_cast<TestRowWidget *>(widget);
    const QString text = index.data(Qt::DisplayRole).toString();
    row->setText(text);
    if (m_heightProvider) {
        row->setPreferredHeight(m_heightProvider(index));
    } else {
        const int height = index.data(StringListModel::HeightRole).toInt();
        row->setPreferredHeight(height);
    }
    ++row->bindCount;
    ++m_bindCount;
    m_boundIndex.insert(widget, QPersistentModelIndex(index));
    m_log.append(QStringLiteral("bind %1").arg(index.row()));
}

void TestAdapter::unbindWidget(QWidget *widget, const QModelIndex &index)
{
    auto *row = static_cast<TestRowWidget *>(widget);
    // Stop everything that is tied to the old index: the label must not keep
    // showing stale data once the widget is pooled.
    row->setText(QString());
    row->setPreferredHeight(0);
    ++row->unbindCount;
    ++m_unbindCount;
    m_log.append(QStringLiteral("unbind %1").arg(index.row()));
    m_boundIndex.remove(widget);
}

QSize TestAdapter::estimatedSize(const QModelIndex &index) const
{
    if (m_heightProvider)
        return QSize(200, m_heightProvider(index));
    return QSize(200, m_estimatedHeight);
}

void TestAdapter::setHeightProvider(std::function<int(const QModelIndex &)> provider)
{
    m_heightProvider = std::move(provider);
}

QPersistentModelIndex TestAdapter::indexOf(const QWidget *widget) const
{
    return m_boundIndex.value(widget);
}

QWidget *TestAdapter::widgetForRow(int row) const
{
    for (auto it = m_boundIndex.constBegin(); it != m_boundIndex.constEnd(); ++it) {
        if (it.value().row() == row)
            return const_cast<QWidget *>(it.key());
    }
    return nullptr;
}

QStringList TestAdapter::visibleTexts() const
{
    QList<QPair<int, QString>> entries;
    entries.reserve(m_boundIndex.size());
    for (auto it = m_boundIndex.constBegin(); it != m_boundIndex.constEnd(); ++it) {
        const auto *row = static_cast<const TestRowWidget *>(it.key());
        entries.append({it.value().row(), row->text()});
    }
    std::sort(entries.begin(), entries.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
    });
    QStringList texts;
    for (const auto &entry : entries)
        texts.append(entry.second);
    return texts;
}

QList<int> TestAdapter::boundRows() const
{
    QList<int> rows;
    rows.reserve(m_boundIndex.size());
    for (auto it = m_boundIndex.constBegin(); it != m_boundIndex.constEnd(); ++it)
        rows.append(it.value().row());
    std::sort(rows.begin(), rows.end());
    return rows;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void showView(QWidget *view, const QSize &size)
{
    view->resize(size);
    view->show();
    QCoreApplication::processEvents();
    if (auto *itemView = qobject_cast<viv::VirtualItemView *>(view))
        itemView->flushPendingRelayout();
    QCoreApplication::processEvents();
}

void settle(int rounds)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

} // namespace vivtest
