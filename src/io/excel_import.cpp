#include "io/excel_import.hpp"
#include "io/export_service.hpp"

#include <QFile>
#include <QLocale>
#include <QRegularExpression>
#include <cmath>
#include <xlsxcell.h>
#include <xlsxdocument.h>

namespace dxfgen {
namespace {
struct CellValue {
    QString text;
    QString error;
    std::optional<double> numeric = std::nullopt;
};
CellValue cell_value(const QXlsx::Document& book, int row, int column) {
    const auto cell = book.cellAt(row, column);
    if (!cell)
        return {};
    const QVariant value = cell->value();
    if (cell->hasFormula() && (!value.isValid() || value.toString().trimmed().isEmpty()))
        return {QStringLiteral("=…"),
                QStringLiteral(
                    "У формулы нет сохранённого результата; пересчитайте и сохраните Excel")};
    if (!value.isValid())
        return {};
    if (cell->cellType() == QXlsx::Cell::ErrorType)
        return {cell->value().toString(), QStringLiteral("Ошибка Excel в ячейке")};
    if (cell->cellType() == QXlsx::Cell::BooleanType)
        return {cell->value().toBool() ? QStringLiteral("TRUE") : QStringLiteral("FALSE"),
                QStringLiteral("Логическое значение вместо текста или числа")};
    if (cell->cellType() == QXlsx::Cell::DateType || cell->isDateTime())
        return {QStringLiteral("Дата/время"),
                QStringLiteral("Дата/время вместо размера или текста")};
    if (cell->cellType() == QXlsx::Cell::NumberType && value.metaType().id() == QMetaType::QString)
        return {value.toString(), QStringLiteral("Некорректное число в ячейке")};
    if (value.metaType().id() == QMetaType::Double)
        return {QString::number(value.toDouble(), 'G',
                                column == 1 ? 15 : QLocale::FloatingPointShortest)
                    .replace('.', ','),
                {},
                value.toDouble()};
    return {value.toString(), {}};
}
bool number(const QString& text, double& result) {
    static const QRegularExpression pattern(
        QStringLiteral("^[+-]?(?:[0-9]+(?:[.,][0-9]*)?|[.,][0-9]+)(?:[eE][+-]?[0-9]+)?$"));
    const auto trimmed = text.trimmed();
    if (!pattern.match(trimmed).hasMatch())
        return false;
    bool ok = false;
    result = QString(trimmed).replace(',', '.').toDouble(&ok);
    return ok && std::isfinite(result);
}
} // namespace

QStringList row_errors(const InputRow& row) {
    auto errors = row.parse_errors;
    if (!row.source_error.isEmpty())
        errors.prepend(row.source_error);
    if (row.part) {
        for (const auto& error : validate(*row.part))
            errors.push_back(QString::fromStdString(error.message));
        if (errors.isEmpty()) {
            const auto filename = cam_file_name(*row.part);
            if (!filename.error.isEmpty())
                errors.push_back(filename.error);
        }
    }
    return errors;
}

QString canonical_shape_type(const QString& text) {
    const auto type = text.trimmed().toLower();
    if (type == QStringLiteral("п") || type == QStringLiteral("прямоугольник"))
        return QStringLiteral("Прямоугольник");
    if (type == QStringLiteral("к") || type == QStringLiteral("круг"))
        return QStringLiteral("Круг");
    if (type == QStringLiteral("ш") || type == QStringLiteral("шайба"))
        return QStringLiteral("Шайба");
    return {};
}

bool valid_import_header(const std::array<QString, 6>& cells) {
    const QStringList prefixes[] = {
        {QStringLiteral("тип"), QStringLiteral("форма")},
        {QStringLiteral("длин"), QStringLiteral("диаметр"), QStringLiteral("наруж")},
        {QStringLiteral("шир"), QStringLiteral("внутр"), QStringLiteral("отверст")},
        {QStringLiteral("толщ")},
        {QStringLiteral("кол")}};
    for (std::size_t column = 1; column < cells.size(); ++column) {
        const auto header = cells[column].trimmed().toLower();
        bool matches = false;
        for (const auto& prefix : prefixes[column - 1])
            matches = matches || header.startsWith(prefix);
        if (!matches)
            return false;
    }
    return true;
}

void parse_row(InputRow& row) {
    row.part.reset();
    row.parse_errors.clear();
    const auto source_suffix = row.source_row ? QString::number(row.source_row) : QString{};
    std::array<CellValue, 6> values;
    for (std::size_t c = 0; c < values.size(); ++c)
        values[c] = {row.cells[c], row.cell_errors[c], row.numeric_values[c]};
    const auto type = canonical_shape_type(row.cells[1]);
    const bool rectangle = type == QStringLiteral("Прямоугольник");
    const bool circle = type == QStringLiteral("Круг");
    const bool washer = type == QStringLiteral("Шайба");
    if (!rectangle && !circle && !washer)
        row.parse_errors.push_back(
            QStringLiteral("B%1: укажите П / К / Ш или Прямоугольник / Круг / Шайба")
                .arg(source_suffix));
    for (int c = 0; c < 6; ++c) {
        if (circle && c == 3)
            continue; // Column D has no meaning for circles, including errors/formulas.
        const auto& error = values[static_cast<std::size_t>(c)].error;
        if (!error.isEmpty())
            row.parse_errors.push_back(
                QStringLiteral("%1%2: %3").arg(QChar('A' + c)).arg(source_suffix).arg(error));
    }
    auto read_number = [&](int c, double default_value, bool optional) {
        const auto& value = values[static_cast<std::size_t>(c)];
        double parsed = default_value;
        if (!value.error.isEmpty())
            return parsed;
        if (optional && value.text.trimmed().isEmpty())
            return parsed;
        bool ok = false;
        if (value.numeric) {
            parsed = *value.numeric;
            ok = std::isfinite(parsed);
        } else {
            ok = number(value.text, parsed);
        }
        if (!ok)
            row.parse_errors.push_back(QStringLiteral("%1%2: требуется число с точкой или запятой")
                                           .arg(QChar('A' + c))
                                           .arg(source_suffix));
        return parsed;
    };
    const double first = read_number(2, 0, false);
    const double second = circle ? 0 : read_number(3, 0, false);
    const double thickness = read_number(4, 0, true);
    const double quantity = read_number(5, 1, true);
    if (values[5].error.isEmpty() &&
        (quantity < 1 || quantity > 1'000'000 || std::floor(quantity) != quantity))
        row.parse_errors.push_back(
            QStringLiteral("F%1: количество должно быть целым от 1 до 1 000 000")
                .arg(source_suffix));
    if (row.parse_errors.isEmpty()) {
        Shape shape = rectangle ? Shape(Rectangle{first, second})
                      : circle  ? Shape(Circle{first})
                                : Shape(Washer{first, second});
        row.part = Part{row.cells[0].toStdString(), std::move(shape), static_cast<int>(quantity),
                        thickness == 0 ? std::nullopt : std::optional<double>(thickness)};
    }
}

ImportResult import_xlsx(const QString& path) {
    ImportResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.size() > 32 * 1024 * 1024) {
        result.error = QStringLiteral("Книга больше 32 МиБ. Разделите список на несколько файлов.");
        return result;
    }
    // Cloud mounts can expose an opaque ID without an extension. Inspect the bytes;
    // QXlsx will then validate that the ZIP actually contains an Excel workbook.
    const auto signature = file.peek(8);
    if (signature.startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1"))) {
        result.error = QStringLiteral("Старый .xls или зашифрованная книга Excel. "
                                      "Сохраните как .xlsx без пароля.");
        return result;
    }
    if (!signature.startsWith(QByteArray::fromHex("504b0304"))) {
        result.error = QStringLiteral("Файл не является книгой .xlsx. "
                                      "Старый .xls сохраните в Excel как .xlsx.");
        return result;
    }
    QXlsx::Document book(&file);
    if (!book.load() || !book.selectSheet(0) || !book.currentWorksheet()) {
        result.error = QStringLiteral("Не удалось прочитать книгу .xlsx и её первый лист. "
                                      "Проверьте файл и отсутствие защиты паролем.");
        return result;
    }
    result.sheet_name = book.sheetNames().value(0);
    result.source_description = QStringLiteral("лист «%1»").arg(result.sheet_name);
    std::array<QString, 6> headers;
    for (int column = 1; column <= 6; ++column)
        headers[static_cast<std::size_t>(column - 1)] = cell_value(book, 1, column).text;
    if (!valid_import_header(headers)) {
        result.error = QStringLiteral("Проверьте заголовки и порядок столбцов A–F: "
                                      "Имя файла | Тип | Длина | Ширина | Толщина | Количество. "
                                      "Данные начинаются со строки 2.");
        return result;
    }
    const int last_row = book.dimension().lastRow();
    if (last_row > 100'001) {
        result.error =
            QStringLiteral("На первом листе больше 100 000 строк данных. Разделите список.");
        return result;
    }
    for (int r = 2; r <= last_row; ++r) {
        InputRow row;
        row.source_row = r;
        std::array<CellValue, 6> values;
        bool blank = true;
        for (int c = 0; c < 6; ++c) {
            values[static_cast<std::size_t>(c)] = cell_value(book, r, c + 1);
            row.cells[static_cast<std::size_t>(c)] = values[static_cast<std::size_t>(c)].text;
            row.cell_errors[static_cast<std::size_t>(c)] =
                values[static_cast<std::size_t>(c)].error;
            row.numeric_values[static_cast<std::size_t>(c)] =
                values[static_cast<std::size_t>(c)].numeric;
            const auto& value = values[static_cast<std::size_t>(c)];
            blank = blank && value.text.trimmed().isEmpty() && value.error.isEmpty();
        }
        if (blank)
            continue;
        parse_row(row);
        result.rows.push_back(std::move(row));
    }
    if (result.rows.empty())
        result.error = QStringLiteral("На первом листе нет строк с деталями в столбцах A–F.");
    return result;
}
} // namespace dxfgen
