#include "desktop/parts_model.hpp"
#include "io/export_service.hpp"

#include <QBrush>
#include <QColor>
#include <algorithm>

namespace dxfgen {
namespace {
QString displayNumber(double value) { return QString::number(value, 'G', 15).replace('.', ','); }
InputRow manualRow(Part part) {
    InputRow row;
    row.cells[0] = QString::fromStdString(part.name);
    row.cells[1] = QString::fromUtf8(shape_name(part.shape));
    if (const auto* rectangle = std::get_if<Rectangle>(&part.shape)) {
        row.cells[2] = displayNumber(rectangle->width);
        row.cells[3] = displayNumber(rectangle->height);
        row.numeric_values[2] = rectangle->width;
        row.numeric_values[3] = rectangle->height;
    } else if (const auto* circle = std::get_if<Circle>(&part.shape)) {
        row.cells[2] = displayNumber(circle->diameter);
        row.numeric_values[2] = circle->diameter;
    } else {
        const auto& washer = std::get<Washer>(part.shape);
        row.cells[2] = displayNumber(washer.outer_diameter);
        row.cells[3] = displayNumber(washer.inner_diameter);
        row.numeric_values[2] = washer.outer_diameter;
        row.numeric_values[3] = washer.inner_diameter;
    }
    row.cells[4] = part.thickness ? displayNumber(*part.thickness) : QString{};
    row.cells[5] = QString::number(part.quantity);
    row.numeric_values[4] = part.thickness;
    row.numeric_values[5] = part.quantity;
    row.part = std::move(part);
    return row;
}
} // namespace
PartsModel::PartsModel(QObject* parent) : QAbstractTableModel(parent) {}
int PartsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}
int PartsModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : columns;
}
bool PartsModel::validRow(int row) const { return row >= 0 && row < rowCount(); }
QVariant PartsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !validRow(index.row()) || index.column() >= columns)
        return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    const bool circle = canonical_shape_type(row.input.cells[1]) == QStringLiteral("Круг");
    if (role == Qt::EditRole && index.column() >= name && index.column() <= quantity)
        return row.input.cells[static_cast<std::size_t>(index.column() - 1)];
    if (role == Qt::CheckStateRole && index.column() == source)
        return row.included ? Qt::Checked : Qt::Unchecked;
    if (role == Qt::ForegroundRole) {
        if (!row.issues.isEmpty())
            return QBrush(QColor("#b45309"));
        if (!row.included)
            return QBrush(QColor("#64748b"));
        if (row.modified && index.column() == status)
            return QBrush(QColor("#2563eb"));
    }
    if (role == Qt::ToolTipRole) {
        const auto origin = row.input.source_row
                                ? QStringLiteral("Строка файла: %1").arg(row.input.source_row)
                                : QStringLiteral("Добавлена вручную");
        const auto message = row.issues.isEmpty() ? row.filename : row.issues.join('\n');
        return origin + (message.isEmpty() ? QString{} : '\n' + message) +
               (row.modified ? QStringLiteral("\nИзменено в программе. Исходный файл не изменён.")
                             : QString{});
    }
    if (role == Qt::AccessibleTextRole || role == Qt::DisplayRole) {
        if (index.column() == source)
            return index.row() + 1;
        if (index.column() >= name && index.column() <= quantity) {
            if (index.column() == type) {
                const auto full = canonical_shape_type(row.input.cells[1]);
                if (!full.isEmpty())
                    return full;
            }
            if (index.column() == second && circle)
                return QStringLiteral("—");
            if (index.column() == quantity && row.input.part)
                return row.input.part->quantity;
            const auto text = row.input.cells[static_cast<std::size_t>(index.column() - 1)];
            return text.isEmpty() ? QStringLiteral("—") : text;
        }
        if (index.column() == filename)
            return row.filename;
        if (index.column() == status)
            return row.issues.isEmpty()
                       ? (row.modified ? QStringLiteral("Изменено") : QStringLiteral("Готово"))
                       : row.issues.join(QStringLiteral("; "));
    }
    return {};
}
QVariant PartsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::ToolTipRole && section == source)
        return QStringLiteral(
            "Порядковый номер детали. Исходная строка файла указана в подсказке.");
    if (role == Qt::ToolTipRole && section >= name && section <= quantity)
        return QStringLiteral("Двойной щелчок или F2 — изменить. Enter или Tab — применить. "
                              "Исходный файл не изменяется.");
    if (role != Qt::DisplayRole)
        return {};
    static const char* labels[] = {"№",       "Название", "Тип",     "Длина / Ø", "Ширина / Ø отв.",
                                   "Толщина", "Кол-во",   "Имя DXF", "Проверка"};
    return section >= 0 && section < columns ? QString::fromUtf8(labels[section]) : QVariant{};
}
Qt::ItemFlags PartsModel::flags(const QModelIndex& index) const {
    if (!index.isValid() || !validRow(index.row()) || index.column() >= columns)
        return Qt::NoItemFlags;
    auto result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == source)
        result |= Qt::ItemIsUserCheckable;
    const auto& input = rows_[static_cast<std::size_t>(index.row())].input;
    const bool circle = canonical_shape_type(input.cells[1]) == QStringLiteral("Круг");
    if (index.column() >= name && index.column() <= quantity &&
        !(index.column() == second && circle) && !(index.column() == name && input.source_row == 0))
        result |= Qt::ItemIsEditable;
    return result;
}
bool PartsModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || !validRow(index.row()) || index.column() >= columns)
        return false;
    auto& row = rows_[static_cast<std::size_t>(index.row())];
    if (role == Qt::CheckStateRole && index.column() == source) {
        const bool included = value.toInt() == Qt::Checked;
        if (row.included == included)
            return true;
        row.included = included;
    } else if (role == Qt::EditRole && flags(index).testFlag(Qt::ItemIsEditable)) {
        const auto c = static_cast<std::size_t>(index.column() - 1);
        const auto text = value.toString();
        if (row.input.cells[c] == text)
            return true;
        row.input.cells[c] = text;
        row.input.cell_errors[c].clear();
        row.input.numeric_values[c].reset();
        parse_row(row.input);
        row.modified = true;
        row.pending_export = true;
    } else {
        return false;
    }
    rebuild();
    return true;
}
const Part* PartsModel::part(int row) const {
    if (!validRow(row))
        return nullptr;
    const auto& value = rows_[static_cast<std::size_t>(row)].input.part;
    return value ? &*value : nullptr;
}
int PartsModel::sourceRow(int row) const {
    return validRow(row) ? rows_[static_cast<std::size_t>(row)].input.source_row : -1;
}
QStringList PartsModel::errors(int row) const {
    return validRow(row) ? rows_[static_cast<std::size_t>(row)].issues : QStringList{};
}
QString PartsModel::fileName(int row) const {
    return validRow(row) ? rows_[static_cast<std::size_t>(row)].filename : QString{};
}
bool PartsModel::isIncluded(int row) const {
    return validRow(row) && rows_[static_cast<std::size_t>(row)].included;
}
bool PartsModel::canExport() const {
    bool any = false;
    for (const auto& row : rows_)
        if (row.included) {
            any = true;
            if (!row.input.part || !row.issues.isEmpty())
                return false;
        }
    return any;
}
bool PartsModel::hasPendingChanges() const {
    return std::any_of(rows_.begin(), rows_.end(),
                       [](const auto& row) { return row.pending_export; });
}
void PartsModel::markExported() {
    for (auto& row : rows_)
        if (row.included)
            row.pending_export = false;
    if (rowCount())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columns - 1));
}
void PartsModel::replace(std::vector<InputRow> rows) {
    beginResetModel();
    rows_.clear();
    for (auto& row : rows)
        rows_.push_back({std::move(row), true, {}, {}});
    rebuild(false);
    endResetModel();
}
void PartsModel::update(int row, Part part) {
    if (!validRow(row))
        return;
    auto& changed = rows_[static_cast<std::size_t>(row)];
    auto& input = changed.input;
    const int source_row = input.source_row;
    input = manualRow(std::move(part));
    input.source_row = source_row;
    changed.modified = true;
    changed.pending_export = true;
    rebuild();
}
int PartsModel::add(Part part) {
    const int row = rowCount();
    beginInsertRows({}, row, row);
    rows_.push_back({manualRow(std::move(part)), true, {}, {}, true, true});
    endInsertRows();
    rebuild();
    return row;
}
void PartsModel::remove(int row) {
    if (!validRow(row))
        return;
    beginRemoveRows({}, row, row);
    rows_.erase(rows_.begin() + row);
    endRemoveRows();
    rebuild();
}
void PartsModel::selectReady() {
    for (auto& row : rows_)
        row.included = row.input.part && row_errors(row.input).isEmpty();
    rebuild();
}
std::vector<Part> PartsModel::included() const {
    std::vector<Part> result;
    if (!canExport())
        return result;
    for (const auto& row : rows_)
        if (row.included)
            result.push_back(*row.input.part);
    return result;
}
void PartsModel::rebuild(bool notify) {
    std::vector<Part> selected;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        row.issues = row_errors(row.input);
        row.filename = row.input.part ? cam_file_name(*row.input.part).name : QString{};
        if (row.included && row.input.part) {
            selected.push_back(*row.input.part);
            indices.push_back(i);
        }
    }
    const auto names = plan_file_names(selected);
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto& row = rows_[indices[i]];
        row.filename = names[i].name;
        if (!names[i].error.isEmpty() && !row.issues.contains(names[i].error))
            row.issues.push_back(names[i].error);
    }
    if (notify && rowCount())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columns - 1));
}
} // namespace dxfgen
