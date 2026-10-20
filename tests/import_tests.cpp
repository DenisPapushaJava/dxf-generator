#include "desktop/parts_model.hpp"
#include "io/excel_import.hpp"
#include "io/export_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
const dxfgen::InputRow& sourceRow(const dxfgen::ImportResult& result, int source) {
    const auto found = std::find_if(result.rows.begin(), result.rows.end(),
                                    [source](const auto& row) { return row.source_row == source; });
    require(found != result.rows.end(), "source row missing");
    return *found;
}
void checkEditing(const QString& root) {
    using namespace dxfgen;
    const auto book = import_xlsx(root + "/examples/Детали.xlsx");
    PartsModel model;
    model.replace(book.rows);
    auto edit = [&](int row, PartsModel::Column column, const QString& value) {
        require(model.setData(model.index(row, column), value, Qt::EditRole), "cell edit rejected");
    };
    require(!model.hasPendingChanges(), "import must not count as an edit");
    for (const auto column : {PartsModel::source, PartsModel::filename, PartsModel::status})
        require(!model.setData(model.index(0, column), "123", Qt::EditRole),
                "derived cell or source row became editable");
    require(!model.flags(model.index(1, PartsModel::second)).testFlag(Qt::ItemIsEditable),
            "circle has an editable second dimension");
    edit(1, PartsModel::quantity, "9");
    require(model.fileName(1) == "К_150_8мм_9шт.dxf" && model.fileName(4) == "К_150_8мм_3шт.dxf",
            "editing did not recalculate duplicate filenames");
    edit(1, PartsModel::quantity, "3");
    require(model.fileName(4) == "К_150_8мм_3шт(1).dxf", "duplicate suffix did not return");
    edit(0, PartsModel::first, "120,5");
    require(model.canExport() && std::get<Rectangle>(model.part(0)->shape).width == 120.5 &&
                model.fileName(0) == "П_120,5_100_6мм_8шт.dxf" && model.sourceRow(0) == 2,
            "edited geometry, filename or original row number is wrong");
    edit(0, PartsModel::first, "abc");
    require(!model.part(0) && !model.canExport() && model.included().empty() &&
                model.data(model.index(0, PartsModel::first), Qt::EditRole) == "abc",
            "invalid edit must stay visible and block stale geometry export");
    edit(0, PartsModel::first, "120.5");
    edit(0, PartsModel::name, "Пластина");
    edit(0, PartsModel::thickness, "2,5");
    require(model.fileName(0) == "Пластина_2,5мм_8шт.dxf", "edited Excel name or thickness lost");
    edit(0, PartsModel::name, "../Пластина");
    require(!model.canExport(), "Windows-invalid edited name accepted");
    edit(0, PartsModel::name, "");
    require(model.fileName(0) == "П_120,5_100_2,5мм_8шт.dxf", "cleared name must be automatic");
    edit(0, PartsModel::type, "Круг");
    require(model.fileName(0) == "К_120,5_2,5мм_8шт.dxf", "edited circle has a second dimension");
    edit(0, PartsModel::type, "Прямоугольник");
    require(std::get<Rectangle>(model.part(0)->shape).height == 100,
            "type switch discarded the previous second dimension");
    model.setData(model.index(0, PartsModel::source), Qt::Unchecked, Qt::CheckStateRole);
    model.markExported();
    require(model.hasPendingChanges(), "export ignored an edited excluded row");
    model.setData(model.index(0, PartsModel::source), Qt::Checked, Qt::CheckStateRole);
    QTemporaryDir output;
    const auto exported = export_parts(output.path(), model.included());
    require(exported.error.isEmpty() && exported.files.size() == 5, "edited list did not export");
    for (int row = 0; row < model.rowCount(); ++row)
        require(QFileInfo(exported.files[row]).fileName() == model.fileName(row),
                "edited names on disk differ from table");
    model.markExported();
    require(!model.hasPendingChanges(), "successful export did not clear pending changes");
    edit(0, PartsModel::quantity, "8");
    require(!model.hasPendingChanges(), "unchanged editor text counts as a new edit");

    const auto typed = import_xlsx(root + "/tests/fixtures/values.xlsx");
    model.replace({sourceRow(typed, 8), sourceRow(typed, 21)});
    edit(0, PartsModel::name, "Точная деталь");
    require(std::get<Circle>(model.part(0)->shape).diameter == 100.12345678901234,
            "editing metadata rounded an untouched numeric diameter");
    edit(1, PartsModel::first, "120");
    require(!model.part(1) && model.errors(1).join(' ').contains("F21"),
            "editing another field rounded fractional Excel quantity into an integer");
    edit(1, PartsModel::quantity, "3");
    require(model.canExport(), "fractional quantity could not be corrected");
    edit(0, PartsModel::quantity, "");
    require(model.part(0)->quantity == 1, "cleared quantity did not default to one");
    edit(0, PartsModel::thickness, "0");
    require(!model.part(0)->thickness, "zero thickness became a filename suffix");

    model.replace({sourceRow(typed, 12)});
    edit(0, PartsModel::quantity, "4");
    require(!model.canExport() && model.errors(0).join(' ').contains("E12"),
            "editing quantity cleared an untouched formula-cache error");
    edit(0, PartsModel::thickness, "2,5");
    require(model.canExport() && model.part(0)->thickness == 2.5,
            "formula error could not be replaced by a literal value");
    model.replace({sourceRow(typed, 3)});
    edit(0, PartsModel::type, "Шайба");
    require(!model.canExport() && model.errors(0).join(' ').contains("D3"),
            "switching from a circle discarded an Excel error in column D");
    edit(0, PartsModel::second, "50");
    require(model.canExport(), "revealed second-dimension error could not be corrected");
    model.replace({});
    model.add(Part{"", Rectangle{100, 50}, 1, {}});
    require(!model.setData(model.index(0, PartsModel::name), "Name", Qt::EditRole),
            "manual name field must remain automatic");
    edit(0, PartsModel::quantity, "2.5");
    require(!model.canExport(), "fractional quantity edit silently truncated");
    model.remove(0);
    require(!model.hasPendingChanges(), "removed pending row still blocks replacement");
}
void check(const QString& root) {
    using namespace dxfgen;
    auto input = import_xlsx(root + "/examples/Детали.xlsx");
    if (!input.error.isEmpty())
        throw std::runtime_error(input.error.toStdString());
    require(input.error.isEmpty() && input.rows.size() == 5, "example workbook not loaded");
    require(input.sheet_name == "Детали", "wrong first sheet");
    QTemporaryDir cloud;
    const auto cloud_path = cloud.filePath("opaque-drive-file-id");
    require(QFile::copy(root + "/examples/Детали.xlsx", cloud_path),
            "cannot prepare extensionless cloud file");
    const auto cloud_input = import_xlsx(cloud_path);
    require(cloud_input.error.isEmpty() && cloud_input.rows.size() == input.rows.size() &&
                cloud_input.sheet_name == input.sheet_name,
            "valid XLSX with an opaque filename rejected");
    require(cam_file_name(*cloud_input.rows[1].part).name == "К_150_8мм_3шт.dxf",
            "extensionless import changes part data");
    const auto legacy_path = cloud.filePath("old-file-id");
    QFile legacy(legacy_path);
    require(legacy.open(QIODevice::WriteOnly), "cannot prepare legacy file");
    legacy.write(QByteArray::fromHex("d0cf11e0a1b11ae1"));
    legacy.close();
    require(import_xlsx(legacy_path).error.contains(".xls"),
            "OLE XLS without an extension needs a format diagnostic");
    PartsModel model;
    model.replace(input.rows);
    require(model.canExport() && model.included().size() == 5, "valid example cannot export");
    require(model.fileName(0) == "П_200_100_6мм_8шт.dxf", "rectangle mapping");
    require(model.fileName(1) == "К_150_8мм_3шт.dxf", "circle mapping");
    require(model.fileName(2) == "Ш_100_50_5мм_10шт.dxf", "washer mapping");
    require(model.fileName(3) == "Кронштейн_2,5мм_2шт.dxf", "source name or text decimals changed");
    require(model.fileName(4) == "К_150_8мм_3шт(1).dxf", "duplicate preview missing");
    model.setData(model.index(1, PartsModel::source), Qt::Unchecked, Qt::CheckStateRole);
    require(model.fileName(4) == "К_150_8мм_3шт.dxf", "selection must update duplicate names");
    model.setData(model.index(1, PartsModel::source), Qt::Checked, Qt::CheckStateRole);
    QTemporaryDir output;
    const auto exported = export_parts(output.path(), model.included());
    require(exported.error.isEmpty() && exported.files.size() == 5, "Excel to DXF export failed");
    for (int i = 0; i < model.rowCount(); ++i)
        require(QFileInfo(exported.files[i]).fileName() == model.fileName(i),
                "actual DXF filename differs from table preview");

    input = import_xlsx(root + "/tests/fixtures/values.xlsx");
    require(input.error.isEmpty() && input.rows.size() == 19,
            "blank rows or typed values mishandled");
    require(sourceRow(input, 3).parse_errors.empty() && row_errors(sourceRow(input, 3)).isEmpty(),
            "column D must be ignored for a circle, even an Excel error");
    require(sourceRow(input, 4).part->quantity == 1 && !sourceRow(input, 4).part->thickness,
            "missing quantity or zero thickness defaults");
    require(sourceRow(input, 6).part && sourceRow(input, 6).part->thickness == 2.5,
            "cached numeric formula results not read");
    require(sourceRow(input, 7).part->name == "Шайба опорная",
            "inline rich-text identifier truncated");
    require(sourceRow(input, 8).part->quantity == 2, "text integer numeric values");
    require(std::get<Circle>(sourceRow(input, 8).part->shape).diameter == 100.12345678901234,
            "numeric geometry must not be rounded to filename precision");
    require(row_errors(sourceRow(input, 9)).join(' ').contains("B9"), "boolean cell accepted");
    require(row_errors(sourceRow(input, 10)).join(' ').contains("C10"),
            "date accepted as diameter");
    require(row_errors(sourceRow(input, 11)).join(' ').contains("E11"),
            "bad numeric metadata became zero");
    require(row_errors(sourceRow(input, 12)).join(' ').contains("результата"),
            "empty cache became zero");
    require(row_errors(sourceRow(input, 13)).join(' ').contains("F13"),
            "missing formula cache became quantity one");
    require(!row_errors(sourceRow(input, 14)).isEmpty(), "Excel formula error accepted");
    require(!sourceRow(input, 15).part, "unknown type became a rectangle");
    require(!sourceRow(input, 16).part, "fractional quantity silently truncated");
    require(!row_errors(sourceRow(input, 17)).isEmpty(), "Windows-invalid identifier accepted");
    require(!row_errors(sourceRow(input, 18)).isEmpty(), "zero dimension accepted");
    require(sourceRow(input, 19).part->name == "123,5", "numeric name formatting");
    require(row_errors(sourceRow(input, 20)).join(' ').contains("E20"),
            "ISO date metadata silently became unspecified thickness");
    require(!sourceRow(input, 21).part, "fractional numeric quantity rounded to an integer");
    model.replace(input.rows);
    require(model.sourceRow(3) == 6, "Excel row numbers renumbered after blank rows");
    require(!model.canExport() && model.included().empty(),
            "invalid rows silently dropped during export");
    model.selectReady();
    require(model.canExport() && model.included().size() == 7, "explicit select-ready action");
    const auto corrected = *model.part(0);
    model.update(0, corrected);
    require(model.sourceRow(0) == 2 && model.part(0)->name == "Пластина",
            "source metadata lost on update");
    QTemporaryDir typed_output;
    const auto typed = export_parts(typed_output.path(), model.included());
    require(typed.error.isEmpty() && typed.files.size() == 7, "typed data export failed");
    int exported_index = 0;
    for (int row = 0; row < model.rowCount(); ++row)
        if (model.isIncluded(row))
            require(QFileInfo(typed.files[exported_index++]).fileName() == model.fileName(row),
                    "selected names differ between model and disk");

    for (const auto* file :
         {"damaged.xlsx", "not-zip.xlsx", "empty.xlsx", "no-header.xlsx", "wrong-order.xlsx"}) {
        const auto failed = import_xlsx(root + "/tests/fixtures/" + file);
        require(!failed.error.isEmpty(), "broken, empty or headerless workbook accepted");
    }
    require(!import_xlsx(root + "/missing.xlsx").error.isEmpty(), "missing file accepted");
    require(!import_xlsx(root + "/old.xls").error.isEmpty(), "legacy XLS accepted");
    require(!import_xlsx(root + "/file.csv").error.isEmpty(), "CSV accepted");
    model.replace({});
    model.add(Part{"", Circle{150}, 3, 8.0});
    require(model.canExport() && model.fileName(0) == "К_150_8мм_3шт.dxf",
            "manual part regression");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        require(app.arguments().size() == 2, "source directory argument required");
        check(app.arguments()[1]);
        checkEditing(app.arguments()[1]);
        std::cout << "XLSX values, errors, row mapping, selection and DXF round-trip passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
