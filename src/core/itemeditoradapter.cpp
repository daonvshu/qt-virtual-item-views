#include <virtualitemviews/itemeditoradapter.h>
#include <QAbstractItemModel>
#include <QMetaProperty>
#include <QWidget>

namespace viv {

EditTriggers ItemEditorAdapter::editTriggers(const QModelIndex &, EditTriggers defaults) const
{
    return defaults;
}

bool ItemEditorAdapter::editRequest(const EditRequestContext &)
{
    return false;
}

void ItemEditorAdapter::cancelEditRequest(const ItemBindingContext &)
{
}

QWidget *ItemEditorAdapter::createEditor(QWidget *, const ItemBindingContext &)
{
    return nullptr;
}

void ItemEditorAdapter::setEditorData(QWidget *editor, const ItemBindingContext &context)
{
    const QMetaProperty property = editor->metaObject()->userProperty();
    if (property.isValid())
        property.write(editor, context.index.data(Qt::EditRole));
}

void ItemEditorAdapter::updateEditorGeometry(QWidget *editor, const QRect &rect,
                                            const ItemBindingContext &)
{
    editor->setGeometry(rect);
}

bool ItemEditorAdapter::commitEditorData(QWidget *editor, QAbstractItemModel *model,
                                        const ItemBindingContext &context)
{
    const QMetaProperty property = editor->metaObject()->userProperty();
    return model && context.index.isValid() && property.isReadable()
        && model->setData(context.index, property.read(editor), Qt::EditRole);
}

void ItemEditorAdapter::destroyEditor(QWidget *editor, const ItemBindingContext &)
{
    editor->deleteLater();
}

} // namespace viv
