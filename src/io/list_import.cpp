#include "io/list_import.hpp"

#include <QFile>
#include <QStringDecoder>

namespace dxfgen {
namespace {
struct Record {
    std::array<QString, 6> cells;
    int columns = 0;
    int line = 1;
    bool blank = true;
};
struct Records {
    std::vector<Record> rows;
    QString error;
};

Records read_records(const QString& text, QChar separator, bool header_only = false) {
    Records result;
    Record record;
    QString field;
    bool quoted = false, closed = false, active = false;
    int line = 1;
    auto end_field = [&] {
        if (record.columns < 6)
            record.cells[static_cast<std::size_t>(record.columns)] = field;
        ++record.columns;
        record.blank = record.blank && field.trimmed().isEmpty();
        field.clear();
        closed = false;
    };
    auto end_record = [&] {
        end_field();
        result.rows.push_back(std::move(record));
        record = Record{};
        record.line = line + 1;
        active = false;
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        const bool newline = ch == '\r' || ch == '\n';
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                    closed = true;
                }
            } else if (newline) {
                field += '\n';
            } else
                field += ch;
        } else if (ch == separator) {
            active = true;
            end_field();
        } else if (newline) {
            end_record();
            if (header_only)
                return result;
        } else if (ch == '"' && field.isEmpty() && !closed) {
            active = true;
            quoted = true;
        } else if (ch == '"' || (closed && ch != ' ')) {
            result.error =
                QStringLiteral("Строка %1: некорректные кавычки в текстовом файле.").arg(line);
            return result;
        } else if (!closed) {
            active = true;
            field += ch;
        }
        if (newline) {
            if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            ++line;
        }
        if (field.size() > 1'048'576 || result.rows.size() > 100'001) {
            result.error = QStringLiteral("Слишком большой список или ячейка. "
                                          "Допускается до 100 000 строк данных.");
            return result;
        }
    }
    if (quoted) {
        result.error = QStringLiteral("Строка %1: не закрыты кавычки.").arg(record.line);
        return result;
    }
    if (active || record.columns)
        end_record();
    if (result.rows.size() > 100'001)
        result.error = QStringLiteral("Больше 100 000 строк данных. Разделите список.");
    return result;
}

QString decode_text(QByteArray bytes, TextEncoding requested, QString& name, QString& error) {
    TextEncoding detected = TextEncoding::automatic;
    int bom_size = 0;
    if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) {
        detected = TextEncoding::utf8;
        bom_size = 3;
    } else if (bytes.startsWith(QByteArray::fromHex("fffe"))) {
        detected = TextEncoding::utf16le;
        bom_size = 2;
    } else if (bytes.startsWith(QByteArray::fromHex("feff"))) {
        detected = TextEncoding::utf16be;
        bom_size = 2;
    }
    if (requested != TextEncoding::automatic && detected != TextEncoding::automatic &&
        requested != detected) {
        error = QStringLiteral("Выбранная кодировка не совпадает с меткой BOM в файле.");
        return {};
    }
    const auto encoding =
        requested == TextEncoding::automatic
            ? (detected == TextEncoding::automatic ? TextEncoding::utf8 : detected)
            : requested;
    bytes.remove(0, bom_size);
    QString text;
    if (encoding == TextEncoding::windows1251) {
        // Unicode mapping of Windows code page 1251; 0x98 is undefined.
        static constexpr char16_t upper[] = {
            0x0402, 0x0403, 0x201a, 0x0453, 0x201e, 0x2026, 0x2020, 0x2021, 0x20ac, 0x2030, 0x0409,
            0x2039, 0x040a, 0x040c, 0x040b, 0x040f, 0x0452, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
            0x2013, 0x2014, 0x0000, 0x2122, 0x0459, 0x203a, 0x045a, 0x045c, 0x045b, 0x045f, 0x00a0,
            0x040e, 0x045e, 0x0408, 0x00a4, 0x0490, 0x00a6, 0x00a7, 0x0401, 0x00a9, 0x0404, 0x00ab,
            0x00ac, 0x00ad, 0x00ae, 0x0407, 0x00b0, 0x00b1, 0x0406, 0x0456, 0x0491, 0x00b5, 0x00b6,
            0x00b7, 0x0451, 0x2116, 0x0454, 0x00bb, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410, 0x0411,
            0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c,
            0x041d, 0x041e, 0x041f, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
            0x0428, 0x0429, 0x042a, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 0x0430, 0x0431, 0x0432,
            0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d,
            0x043e, 0x043f, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448,
            0x0449, 0x044a, 0x044b, 0x044c, 0x044d, 0x044e, 0x044f};
        name = QStringLiteral("Windows-1251");
        text.reserve(bytes.size());
        for (unsigned char ch : bytes) {
            const auto value = ch < 128 ? static_cast<char16_t>(ch) : upper[ch - 128];
            if (!value) {
                error = QStringLiteral("Файл содержит недопустимый байт Windows-1251.");
                return {};
            }
            text += QChar(value);
        }
    } else {
        const auto codec = encoding == TextEncoding::utf16le   ? QStringConverter::Utf16LE
                           : encoding == TextEncoding::utf16be ? QStringConverter::Utf16BE
                                                               : QStringConverter::Utf8;
        name = encoding == TextEncoding::utf16le   ? QStringLiteral("UTF-16 LE")
               : encoding == TextEncoding::utf16be ? QStringLiteral("UTF-16 BE")
                                                   : QStringLiteral("UTF-8");
        QStringDecoder decoder(codec, QStringConverter::Flag::Stateless);
        text = decoder(bytes);
        if (decoder.hasError()) {
            error = QStringLiteral("Не удалось прочитать %1. Выберите кодировку файла.").arg(name);
            return {};
        }
    }
    for (const auto ch : text)
        if ((ch.unicode() < 32 && ch != '\t' && ch != '\r' && ch != '\n') || ch.unicode() == 0x7f) {
            error = QStringLiteral("Файл содержит двоичные данные или неверно выбрана кодировка.");
            return {};
        }
    return text;
}
} // namespace

ImportResult import_list(const QString& path, TextImportOptions options) {
    ImportResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    constexpr qint64 limit = 32 * 1024 * 1024;
    if (file.size() > limit) {
        result.error = QStringLiteral("Файл больше 32 МиБ. Разделите список.");
        return result;
    }
    const auto signature = file.peek(8);
    if (signature.startsWith(QByteArray::fromHex("504b0304")) ||
        signature.startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1"))) {
        file.close();
        return import_xlsx(path);
    }
    const auto bytes = file.read(limit + 1);
    if (bytes.size() > limit || file.error() != QFileDevice::NoError) {
        result.error =
            QStringLiteral("Не удалось прочитать файл целиком или превышен размер 32 МиБ.");
        return result;
    }
    QString encoding_name;
    const auto text = decode_text(bytes, options.encoding, encoding_name, result.error);
    if (!result.error.isEmpty()) {
        // Offer parameters only for plausible tables, not arbitrary binary files.
        if (options.encoding == TextEncoding::automatic)
            for (const auto alternative :
                 {TextEncoding::windows1251, TextEncoding::utf16le, TextEncoding::utf16be}) {
                QString name, error;
                const auto candidate = decode_text(bytes, alternative, name, error);
                if (!error.isEmpty())
                    continue;
                for (const auto delimiter : {QChar(';'), QChar(','), QChar('\t')}) {
                    const auto header = read_records(candidate, delimiter, true);
                    if (header.error.isEmpty() && !header.rows.empty() &&
                        header.rows[0].columns == 6 && valid_import_header(header.rows[0].cells))
                        result.needs_text_options = true;
                }
                if (result.needs_text_options)
                    break;
            }
        return result;
    }
    QChar separator = options.separator;
    if (separator.isNull()) {
        int matches = 0;
        for (const auto candidate : {QChar(';'), QChar(','), QChar('\t')}) {
            const auto header = read_records(text, candidate, true);
            if (header.error.isEmpty() && !header.rows.empty() && header.rows[0].columns == 6 &&
                valid_import_header(header.rows[0].cells)) {
                separator = candidate;
                ++matches;
            }
        }
        if (matches != 1) {
            result.error =
                QStringLiteral("Не удалось определить таблицу. Нужны заголовки A–F: "
                               "Имя файла | Тип | Длина | Ширина | Толщина | Количество. "
                               "Поддерживаются XLSX, CSV и TSV; старый XLS сохраните как XLSX.");
            // A readable header with an uncertain delimiter can be adjusted explicitly.
            result.needs_text_options =
                text.left(4096).contains(QStringLiteral("Тип"), Qt::CaseInsensitive) ||
                text.left(4096).contains(QStringLiteral("Форма"), Qt::CaseInsensitive);
            return result;
        }
    }
    if (separator != ';' && separator != ',' && separator != '\t') {
        result.error =
            QStringLiteral("Допустимые разделители: точка с запятой, запятая, табуляция.");
        return result;
    }
    auto parsed = read_records(text, separator);
    if (!parsed.error.isEmpty()) {
        result.error = parsed.error;
        return result;
    }
    if (parsed.rows.empty() || parsed.rows[0].columns != 6 ||
        !valid_import_header(parsed.rows[0].cells)) {
        result.error = QStringLiteral("Проверьте заголовки и порядок шести столбцов A–F. "
                                      "Данные начинаются со строки 2.");
        return result;
    }
    result.source_description =
        QStringLiteral("%1 · %2 · %3")
            .arg(separator == '\t' ? QStringLiteral("TSV") : QStringLiteral("CSV"), encoding_name,
                 separator == '\t' ? QStringLiteral("табуляция")
                                   : QStringLiteral("разделитель «%1»").arg(separator));
    for (std::size_t i = 1; i < parsed.rows.size(); ++i) {
        auto& record = parsed.rows[i];
        if (record.blank)
            continue;
        InputRow row;
        row.source_row = record.line;
        row.cells = std::move(record.cells);
        if (record.columns != 6)
            row.source_error =
                QStringLiteral("Строка %1: столбцов %2 вместо 6. Исправьте файл или разделитель.")
                    .arg(record.line)
                    .arg(record.columns);
        parse_row(row);
        result.rows.push_back(std::move(row));
    }
    if (result.rows.empty())
        result.error = QStringLiteral("В файле нет строк с деталями.");
    return result;
}
} // namespace dxfgen
