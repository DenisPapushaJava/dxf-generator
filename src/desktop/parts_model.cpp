#include "desktop/parts_model.hpp"

#include <QBrush>
#include <QColor>

namespace dxfgen {
PartsModel::PartsModel(QObject* parent) : QAbstractTableModel(parent) {
    rows_.push_back({{"Шайба опорная", Washer{}, 8, 6.0}, true});
}
int PartsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}
int PartsModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 3; }
QVariant PartsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !part(index.row()))
        return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    const auto errors = validate(row.part);
    if (role == Qt::CheckStateRole && index.column() == 0)
        return row.included ? Qt::Checked : Qt::Unchecked;
    if (role == Qt::ForegroundRole && !errors.empty())
        return QBrush(QColor("#b45309"));
    if (role == Qt::ToolTipRole && !errors.empty())
        return QString::fromStdString(errors.front().message);
    if (role == Qt::AccessibleTextRole || role == Qt::DisplayRole) {
        if (index.column() == 0)
            return QString::fromStdString(row.part.name);
        if (index.column() == 1)
            return QString::fromUtf8(shape_name(row.part.shape));
        if (index.column() == 2)
            return row.part.quantity;
    }
    return {};
}
QVariant PartsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const char* labels[] = {"Деталь", "Тип", "Шт."};
    return section >= 0 && section < 3 ? QString::fromUtf8(labels[section]) : QVariant{};
}
Qt::ItemFlags PartsModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;
    auto result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 0)
        result |= Qt::ItemIsUserCheckable;
    return result;
}
bool PartsModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || !part(index.row()) || index.column() != 0 || role != Qt::CheckStateRole)
        return false;
    rows_[static_cast<std::size_t>(index.row())].included = value.toInt() == Qt::Checked;
    emit dataChanged(index, index, {Qt::CheckStateRole});
    return true;
}
const Part* PartsModel::part(int row) const {
    return row >= 0 && row < rowCount() ? &rows_[static_cast<std::size_t>(row)].part : nullptr;
}
void PartsModel::update(int row, Part value) {
    if (!part(row))
        return;
    rows_[static_cast<std::size_t>(row)].part = std::move(value);
    emit dataChanged(index(row, 0), index(row, 2));
}
int PartsModel::add(Shape shape) {
    const int row = rowCount();
    beginInsertRows({}, row, row);
    Part value{
        std::string(shape_name(shape)) + " " + std::to_string(row + 1), std::move(shape), 1, {}};
    rows_.push_back({std::move(value), true});
    endInsertRows();
    return row;
}
void PartsModel::remove(int row) {
    if (!part(row))
        return;
    beginRemoveRows({}, row, row);
    rows_.erase(rows_.begin() + row);
    endRemoveRows();
}
std::vector<Part> PartsModel::included() const {
    std::vector<Part> result;
    for (const auto& row : rows_)
        if (row.included)
            result.push_back(row.part);
    return result;
}
} // namespace dxfgen
