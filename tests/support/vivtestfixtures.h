#pragma once

#include <virtualitemviews/widgetadapter.h>
#include <virtualitemviews/virtuallistview.h>

#include <QAbstractListModel>
#include <QLabel>
#include <QPersistentModelIndex>
#include <QStringList>
#include <QWidget>

#include <functional>

namespace vivtest {

/// Mutable string list model used as the data source of the tests.
class StringListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /// Role returning the preferred height of a row (0 = no preference).
    static constexpr int HeightRole = Qt::UserRole + 1;

    explicit StringListModel(QObject *parent = nullptr);
    explicit StringListModel(const QStringList &rows, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    const QStringList &rows() const { return m_rows; }

    void appendRow(const QString &text);
    void insertRowsAt(int first, const QStringList &texts);
    bool removeRowsAt(int first, int count);
    bool moveRow(int from, int to);
    void setRowText(int row, const QString &text);
    void setRowHeight(int row, int height);
    /// Full reset (modelAboutToBeReset/modelReset).
    void replaceAll(const QStringList &rows);
    /// Reorders the rows and remaps the persistent indexes (sort/filter case).
    void reverseKeepingPersistentIndexes();

private:
    QStringList m_rows;
    QHash<int, int> m_heights;
};

/// Minimal business row widget: a label plus lifecycle counters.
class NumericListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit NumericListModel(int count = 0, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    int count() const { return m_count; }
    void setCount(int count);
    void appendRows(int count);
    bool removeRowsAt(int first, int count);
    bool moveRow(int from, int to);

    /// Text of a row; the identity of a row is its index for this model.
    static QString textForRow(int row);

private:
    int m_count = 0;
};

/// Minimal business row widget: a label plus lifecycle counters.
class TestRowWidget : public QWidget
{
public:
    explicit TestRowWidget(QWidget *parent = nullptr, int preferredHeight = 0);

    QLabel *label() const { return m_label; }
    QString text() const;
    void setText(const QString &text);

    QSize sizeHint() const override;
    void setPreferredHeight(int height);
    int preferredHeight() const { return m_preferredHeight; }

    int bindCount = 0;
    int unbindCount = 0;

private:
    QLabel *m_label = nullptr;
    int m_preferredHeight = 0;
};

/// Instrumented WidgetAdapter: counts creation/bind/unbind and remembers which
/// index every widget is currently bound to.
class TestAdapter : public viv::WidgetAdapter
{
public:
    explicit TestAdapter(int estimatedHeight = 24);

    viv::WidgetType widgetType(const QModelIndex &index) const override;
    QWidget *createWidget(viv::WidgetType type, QWidget *parent) override;
    void bindWidget(QWidget *widget, const QModelIndex &index) override;
    void unbindWidget(QWidget *widget, const QModelIndex &index) override;
    QSize estimatedSize(const QModelIndex &index) const override;

    int createdCount() const { return m_createdCount; }
    int bindCount() const { return m_bindCount; }
    int unbindCount() const { return m_unbindCount; }
    int boundWidgetCount() const { return m_boundIndex.size(); }

    void setEstimatedHeight(int height) { m_estimatedHeight = height; }
    void setTypeCount(int count) { m_typeCount = qMax(1, count); }
    void setHeightProvider(std::function<int(const QModelIndex &)> provider);

    QPersistentModelIndex indexOf(const QWidget *widget) const;
    QWidget *widgetForRow(int row) const;
    /// Texts of all bound widgets, ordered by row.
    QStringList visibleTexts() const;
    QList<int> boundRows() const;
    /// Lifecycle log: "bind <row>" / "unbind <row>".
    const QStringList &lifecycleLog() const { return m_log; }

private:
    QHash<const QWidget *, QPersistentModelIndex> m_boundIndex;
    QStringList m_log;
    std::function<int(const QModelIndex &)> m_heightProvider;
    int m_createdCount = 0;
    int m_bindCount = 0;
    int m_unbindCount = 0;
    int m_estimatedHeight = 24;
    int m_typeCount = 1;
};

/// Creates a view, shows it offscreen at \a size and settles the layout.
void showView(QWidget *view, const QSize &size = QSize(400, 300));

/// Spins the event loop so that queued relayouts and deleteLater() run.
void settle(int rounds = 3);

} // namespace vivtest
