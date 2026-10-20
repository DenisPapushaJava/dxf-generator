#include "desktop/parts_model.hpp"
#include "io/export_service.hpp"
#include "io/list_import.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void check(const QString& root) {
    using namespace dxfgen;
    QTemporaryDir temp;
    auto read = [&](const QByteArray& bytes, TextImportOptions options = {}) {
        const auto path = temp.filePath("opaque-file");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                "write fixture");
        file.close();
        return import_list(path, options);
    };
    const QString header = QStringLiteral("Имя файла;Тип;Длина;Ширина;Толщина;Количество\r\n");
    const auto text =
        header +
        QStringLiteral("; п ;200;100;6;8\r\n;к;150;ошибка;8;3\r\n"
                       ";Ш;100;50;5;10\r\nКронштейн;П;120,5;80.25;2,5;2\r\n\r\n;К;150;;8;3\r\n");
    const auto input = read(text.toUtf8());
    require(input.error.isEmpty() && input.rows.size() == 5 && input.rows[4].source_row == 7,
            "short types, mixed decimals or blank row mapping failed");
    PartsModel model;
    model.replace(input.rows);
    require(model.canExport() && model.fileName(0) == "П_200_100_6мм_8шт.dxf" &&
                model.fileName(1) == "К_150_8мм_3шт.dxf" &&
                model.fileName(2) == "Ш_100_50_5мм_10шт.dxf" &&
                model.fileName(3) == "Кронштейн_2,5мм_2шт.dxf" &&
                model.fileName(4) == "К_150_8мм_3шт(1).dxf",
            "CAM names changed");
    require(model.data(model.index(1, PartsModel::type)).toString() == "Круг" &&
                model.data(model.index(1, PartsModel::type), Qt::EditRole).toString() == "к" &&
                !model.flags(model.index(1, PartsModel::second)).testFlag(Qt::ItemIsEditable),
            "circle alias display/edit semantics failed");
    const auto exported = export_parts(temp.path(), model.included());
    require(exported.error.isEmpty() && exported.files.size() == 5, "text to DXF export failed");
    require(read(QByteArray::fromHex("efbbbf") + text.toUtf8()).error.isEmpty(), "UTF8 BOM failed");
    for (auto encoding : {QStringConverter::Utf16LE, QStringConverter::Utf16BE}) {
        QStringEncoder encoder(encoding);
        const QByteArray bytes = encoder(text);
        const auto bom = encoding == QStringConverter::Utf16LE ? QByteArray::fromHex("fffe")
                                                               : QByteArray::fromHex("feff");
        require(read(bom + bytes).rows.size() == 5, "UTF16 BOM failed");
        const auto selected =
            encoding == QStringConverter::Utf16LE ? TextEncoding::utf16le : TextEncoding::utf16be;
        require(read(bytes, {selected, {}}).rows.size() == 5, "explicit UTF16 failed");
    }
    QByteArray cp;
    for (const auto ch : text) {
        const auto u = ch.unicode();
        require(u < 128 || (u >= 0x410 && u <= 0x44f), "fixture not encodable");
        cp += static_cast<char>(u < 128 ? u : u - 0x410 + 0xc0);
    }
    require(read(cp).needs_text_options, "legacy encoding silently guessed");
    const auto legacy = read(cp, {TextEncoding::windows1251, {}});
    require(legacy.error.isEmpty() && legacy.rows[3].cells[0] == "Кронштейн", "Windows1251 failed");
    require(!read(cp + char(0x98), {TextEncoding::windows1251, {}}).error.isEmpty(),
            "undefined byte accepted");
    require(!read(QByteArray::fromHex("efbbbf") + text.toUtf8(), {TextEncoding::windows1251, {}})
                 .error.isEmpty(),
            "conflicting BOM accepted");
    const auto csv =
        QStringLiteral("Имя файла,Тип,Длина,Ширина,Толщина,Количество\n"
                       "\"Деталь; №1\",П,\"120,5\",80,\"2,5\",2\n\"Деталь \"\"А\"\"\",К,150,,8,3");
    const auto quoted = read(csv.toUtf8());
    require(quoted.error.isEmpty() && quoted.rows.size() == 2 && quoted.rows[0].part &&
                quoted.rows[0].part->thickness == 2.5 && quoted.rows[0].cells[0] == "Деталь; №1" &&
                quoted.rows[1].cells[0] == "Деталь \"А\"",
            "quoted separators, decimals or escaped quotes failed");
    require(!row_errors(quoted.rows[1]).isEmpty(), "invalid filename not retained");
    const auto multiline =
        read((header + QStringLiteral("\"Имя\r\nстрока\";К;150;;8;3\r\n;К;160;;8;3")).toUtf8());
    require(multiline.error.isEmpty() && multiline.rows.size() == 2 &&
                multiline.rows[1].source_row == 4 && !row_errors(multiline.rows[0]).isEmpty(),
            "multiline record or physical source numbers lost");
    for (const auto& bad :
         {QStringLiteral(";К;150;;8;3;лишнее"), QStringLiteral(";К;150;;8"),
          QStringLiteral(";Неизвестно;150;;8;3"), QStringLiteral(";П;abc;80;6;3")}) {
        const auto parsed = read((header + bad).toUtf8());
        require(parsed.error.isEmpty() && parsed.rows.size() == 1 &&
                    !row_errors(parsed.rows[0]).isEmpty(),
                "invalid row was dropped or accepted");
        model.replace(parsed.rows);
        require(!model.canExport(), "invalid text row exportable");
    }
    for (const auto& bad :
         {QStringLiteral("\"bad;К;150;;8;3"), QStringLiteral("\"Имя\"x;К;150;;8;3"),
          QStringLiteral("Им\"я;К;150;;8;3")})
        require(!read((header + bad).toUtf8()).error.isEmpty(), "malformed quotes accepted");
    require(!read(header.toUtf8()).error.isEmpty(), "empty list accepted");
    const auto binary = read(QByteArray("binary\0data", 11));
    require(!binary.error.isEmpty() && !binary.needs_text_options,
            "binary accepted or offered text parameters");
    require(!read(header.toUtf8() + QByteArray(";К;150;;8;3\xff")).error.isEmpty(),
            "invalid UTF8 silently replaced");
    require(
        !read((header + QStringLiteral(";К;150;;8;3")).toUtf8(), {TextEncoding::utf8, QChar('|')})
             .error.isEmpty(),
        "unsupported delimiter accepted");
    for (const auto* file : {"Детали.xlsx", "Детали.csv", "Детали.tsv"}) {
        const auto example = import_list(root + "/examples/" + QString::fromUtf8(file));
        require(example.error.isEmpty() && example.rows.size() == 5 &&
                    example.rows[0].cells[1] == "П" && example.rows[1].cells[1] == "К" &&
                    example.rows[2].cells[1] == "Ш",
                "example short types not readable");
        model.replace(example.rows);
        require(model.canExport() && model.fileName(4) == "К_150_8мм_3шт(1).dxf",
                "example names inconsistent");
    }
    const auto old = read(QByteArray::fromHex("d0cf11e0a1b11ae1"));
    require(!old.error.isEmpty() && old.error.contains(".xls"), "legacy XLS misdetected");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        require(app.arguments().size() == 2, "source directory argument required");
        check(app.arguments()[1]);
        std::cout << "Text imports, encodings, aliases, row errors and CAM exports passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
