#include <virtualitemviews/virtualtreeview.h>
#include <virtualitemviews/virtualtreetableview.h>
#include <virtualitemviews/virtualheaderview.h>
#include "vivtestfixtures.h"

#include <QtTest>
#include <QLabel>
#include <QStandardItemModel>
#include <type_traits>

namespace {

class RowAdapter : public viv::TableWidgetAdapter
{
public:
    QWidget *createWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }
    void bindWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
    QSize estimatedSize(const QModelIndex &) const override { return QSize(200, 24); }
};

class CellAdapter : public viv::CellWidgetAdapter
{
public:
    QWidget *createCellWidget(viv::WidgetType, QWidget *parent) override
    {
        return new QLabel(parent);
    }
    void bindCellWidget(QWidget *widget, const QModelIndex &index) override
    {
        static_cast<QLabel *>(widget)->setText(index.data().toString());
    }
};

template<typename View>
void exerciseSpacing(bool cells)
{
    QStandardItemModel model;
    auto *p = new QStandardItem(QStringLiteral("P"));
    auto *a = new QStandardItem(QStringLiteral("a"));
    auto *g = new QStandardItem(QStringLiteral("g"));
    auto *b = new QStandardItem(QStringLiteral("b"));
    auto *q = new QStandardItem(QStringLiteral("Q"));
    auto *childQ = new QStandardItem(QStringLiteral("q"));
    auto *r = new QStandardItem(QStringLiteral("R"));
    a->appendRow(g);
    p->appendRow(a);
    p->appendRow(b);
    q->appendRow(childQ);
    model.appendRow(p);
    model.appendRow(q);
    model.appendRow(r);
    p->setData(11, View::NodeRowSpacingRole);
    a->setData(2, View::NodeRowSpacingRole);
    g->setData(7, View::NodeRowSpacingRole);
    b->setData(0, View::NodeRowSpacingRole);
    q->setData(13, View::NodeRowSpacingRole);

    View view;
    view.setAdapter(new RowAdapter, true);
    if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>) {
        view.setCellAdapter(new CellAdapter, true);
        if (cells)
            view.setMaterializationMode(viv::VirtualTableView::MaterializationMode::CellWidgets);
    }
    view.setUniformItemHeight(24);
    view.setModel(&model);
    view.setRowSpacing(4);
    vivtest::showView(&view, QSize(500, 500));

    const auto verify = [&](const QList<QStandardItem *> &nodes, const QVector<int> &gaps) {
        view.flushPendingRelayout();
        QCOMPARE(view.visibleRowCount(), qsizetype(nodes.size()));
        for (int i = 0; i < nodes.size(); ++i) {
            const QRect body = view.visualRect(nodes.at(i)->index());
            QVERIFY(body.isValid());
            if constexpr (std::is_same_v<View, viv::VirtualTreeTableView>) {
                if (i + 1 < nodes.size())
                    QCOMPARE(view.verticalHeaderGeometry()->sectionSpacingAfter(i), gaps.at(i));
                auto *header = dynamic_cast<viv::VirtualHeaderView *>(view.verticalHeader());
                QVERIFY(header);
                QWidget *section = header->sectionWidget(i);
                QVERIFY(section);
                QCOMPARE(header->y() + section->y(), view.viewport()->y() + body.y());
            }
            if (i + 1 < nodes.size()) {
                const int gap = gaps.at(i);
                QCOMPARE(view.visualRect(nodes.at(i + 1)->index()).top() - body.bottom() - 1, gap);
                if (gap > 0)
                    QCOMPARE(static_cast<const viv::VirtualItemView &>(view).indexAt(
                                 QPoint(body.center().x(), body.bottom() + 1)), QModelIndex());
            }
        }
        QCOMPARE(view.contentExtent(), qint64(view.visualRect(nodes.last()->index()).bottom() + 1));
    };

    verify({p, q, r}, {11, 13});
    p->setData(30, View::NodeRowSpacingAboveRole);
    q->setData(5, View::NodeRowSpacingAboveRole);
    r->setData(8, View::NodeRowSpacingAboveRole);
    verify({p, q, r}, {16, 21});
    QCOMPARE(view.visualRect(p->index()).top(), 0);
    view.expand(p->index());
    verify({p, a, b, q, r}, {11, 2, 5, 21});
    a->setData(3, View::NodeRowSpacingAboveRole);
    verify({p, a, b, q, r}, {14, 2, 5, 21});
    view.expand(a->index());
    g->setData(6, View::NodeRowSpacingAboveRole);
    verify({p, a, g, b, q, r}, {14, 8, 7, 5, 21});
    view.collapse(p->index());
    verify({p, q, r}, {16, 21});
    q->setData(-1, View::NodeRowSpacingAboveRole);
    verify({p, q, r}, {11, 21});
    for (QStandardItem *node : {p, a, g, q, r})
        node->setData(QVariant(), View::NodeRowSpacingAboveRole);
    view.collapse(a->index());
    verify({p, q, r}, {11, 13});
    view.expand(p->index());
    verify({p, a, b, q, r}, {11, 2, 0, 13});
    view.expand(a->index());
    verify({p, a, g, b, q, r}, {11, 2, 7, 0, 13});
    view.expand(q->index());
    verify({p, a, g, b, q, childQ, r}, {11, 2, 7, 0, 13, 4});
    view.collapse(p->index());
    verify({p, q, childQ, r}, {11, 13, 4});
    view.expand(p->index());
    verify({p, a, g, b, q, childQ, r}, {11, 2, 7, 0, 13, 4});
    a->setData(0, View::NodeRowSpacingRole);
    verify({p, a, g, b, q, childQ, r}, {11, 0, 7, 0, 13, 4});
    a->setData(-1, View::NodeRowSpacingRole);
    g->setData(QVariant(), View::NodeRowSpacingRole);
    verify({p, a, g, b, q, childQ, r}, {11, 4, 4, 0, 13, 4});
    view.collapse(a->index());
    verify({p, a, b, q, childQ, r}, {11, 4, 0, 13, 4});
    view.setRowSpacing(6);
    verify({p, a, b, q, childQ, r}, {11, 6, 0, 13, 6});
    view.setRootIndex(p->index());
    verify({a, b}, {6});
    view.expand(a->index());
    verify({a, g, b}, {6, 6});
    auto *added = new QStandardItem(QStringLiteral("added"));
    added->setData(15, View::NodeRowSpacingRole);
    added->setData(7, View::NodeRowSpacingAboveRole);
    p->insertRow(1, added);
    verify({a, g, added, b}, {6, 13, 15});
    p->removeRow(1);
    verify({a, g, b}, {6, 6});
    view.setRootIndex(QModelIndex());
    view.collapseAll();
    verify({p, q, r}, {11, 13});
    model.clear();
    view.flushPendingRelayout();
    QCOMPARE(view.visibleRowCount(), qsizetype(0));
}

} // namespace

class TestTreeRowSpacing : public QObject
{
    Q_OBJECT
private slots:
    void relationshipSpacing_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("tree") << 0;
        QTest::newRow("tree-table-rows") << 1;
        QTest::newRow("tree-table-cells") << 2;
    }
    void relationshipSpacing()
    {
        QFETCH(int, mode);
        if (mode == 0)
            exerciseSpacing<viv::VirtualTreeView>(false);
        else
            exerciseSpacing<viv::VirtualTreeTableView>(mode == 2);
    }
};

QTEST_MAIN(TestTreeRowSpacing)
#include "tst_treerowspacing.moc"
