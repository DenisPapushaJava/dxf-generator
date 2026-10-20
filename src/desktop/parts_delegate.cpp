#include "desktop/parts_delegate.hpp"
#include "desktop/parts_model.hpp"

#include <QComboBox>

namespace dxfgen {
QWidget* PartsDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    QWidget* editor;
    if (index.column() == PartsModel::type) {
        auto* combo = new QComboBox(parent);
        combo->setObjectName(QStringLiteral("partTypeEditor"));
        combo->addItems(
            {QStringLiteral("Прямоугольник"), QStringLiteral("Круг"), QStringLiteral("Шайба")});
        auto* delegate = const_cast<PartsDelegate*>(this);
        connect(combo, &QComboBox::activated, delegate, [delegate, combo] {
            emit delegate->commitData(combo);
            emit delegate->closeEditor(combo, QAbstractItemDelegate::NoHint);
        });
        editor = combo;
    } else {
        editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (!editor)
            return nullptr;
        editor->setObjectName(QStringLiteral("partCellEditor"));
    }
    editor->setAcceptDrops(false);
    for (auto* child : editor->findChildren<QWidget*>())
        child->setAcceptDrops(false);
    return editor;
}
void PartsDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        const auto value = index.data(Qt::EditRole).toString();
        const auto full = canonical_shape_type(value);
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i) == full) {
                combo->setCurrentIndex(i);
                return;
            }
        // Keep an unknown Excel value intact until the user chooses a valid type.
        combo->insertItem(0, value);
        combo->setCurrentIndex(0);
    } else {
        QStyledItemDelegate::setEditorData(editor, index);
    }
}
void PartsDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                 const QModelIndex& index) const {
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        if (canonical_shape_type(index.data(Qt::EditRole).toString()) != combo->currentText())
            model->setData(index, combo->currentText(), Qt::EditRole);
    } else
        QStyledItemDelegate::setModelData(editor, model, index);
}
} // namespace dxfgen
