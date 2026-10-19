#include "io/export_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void check_names() {
    using namespace dxfgen;
    const struct {
        Part part;
        const char* expected;
    } examples[] = {
        {{"Пластина", Rectangle{200, 100}, 8, 6.0}, "Пластина_6мм_8шт.dxf"},
        {{"Шайба опорная", Washer{100, 50}, 8, 6.0}, "Шайба опорная_6мм_8шт.dxf"},
        {{"Диск", Circle{150}, 3, 2.5}, "Диск_2,5мм_3шт.dxf"},
        {{"Диск", Circle{150}, 3, 0.00001}, "Диск_1E-05мм_3шт.dxf"},
        {{"", Rectangle{200, 100}, 8, 6.0}, "П_200_100_6мм_8шт.dxf"},
        {{"", Circle{150}, 3, 8.0}, "К_150_8мм_3шт.dxf"},
        {{"", Circle{150.5}, 3, 2.5}, "К_150,5_2,5мм_3шт.dxf"},
        {{"", Washer{100, 50}, 10, 5.0}, "Ш_100_50_5мм_10шт.dxf"},
        {{"", Rectangle{200.5, 100.25}, 8, 2.5}, "П_200,5_100,25_2,5мм_8шт.dxf"},
        {{"Пластина", Rectangle{}, 8, {}}, "Пластина_8шт.dxf"},
        {{"", Circle{20}, 1, {}}, "К_20_1шт.dxf"},
        {{"  Имя с пробелами ", Rectangle{}, 1, {}}, "  Имя с пробелами _1шт.dxf"},
        {{"Деталь.dxf", Rectangle{}, 1, {}}, "Деталь.dxf_1шт.dxf"},
        {{"CON", Rectangle{}, 1, {}}, "CON_1шт.dxf"},
    };
    const auto previous = QLocale();
    for (const auto& locale : {QLocale::c(), QLocale(QLocale::English, QLocale::UnitedStates),
                               QLocale(QLocale::Russian, QLocale::Russia)}) {
        QLocale::setDefault(locale);
        for (const auto& example : examples) {
            const auto result = cam_file_name(example.part);
            require(result.error.isEmpty(), "valid legacy filename rejected");
            require(result.name == QString::fromUtf8(example.expected), "CAM filename mismatch");
        }
    }
    QLocale::setDefault(previous);

    Part invalid{"../escape", Rectangle{}, 1, {}};
    for (const auto* name :
         {"../escape", "sub\\file", "bad:name", "bad?name", "bad*name", "bad\"name", "bad<name",
          "bad>name", "bad|name", "bad\nname", "CON.txt", "aux.txt", "COM¹.txt", " \t", "\u00a0"}) {
        invalid.name = name;
        const auto result = cam_file_name(invalid);
        require(!result.error.isEmpty() && result.name.isEmpty(),
                "invalid Windows filename must fail instead of silently changing identifier");
    }
    invalid.name = std::string(248, 'a'); // 248 + _1шт.dxf = 256 UTF-16 units.
    require(!cam_file_name(invalid).error.isEmpty(), "long name must not be silently truncated");
    invalid.name = std::string(247, 'a');
    require(cam_file_name(invalid).name.size() == 255, "valid filename length boundary");
}
void check_export() {
    using namespace dxfgen;
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory");
    const QDir dir(temp.path());
    Part part{"plate", Rectangle{}, 2, {}};
    auto upper = part;
    upper.name = "PLATE";
    auto exported = export_parts(dir.path(), {part, part, upper});
    require(exported.error.isEmpty() && exported.files.size() == 3, "batch export failed");
    require(QFileInfo(exported.files[0]).fileName() == QString::fromUtf8("plate_2шт.dxf"),
            "first filename changed");
    require(QFileInfo(exported.files[1]).fileName() == QString::fromUtf8("plate_2шт(1).dxf"),
            "first duplicate must retain legacy (1) suffix");
    require(QFileInfo(exported.files[2]).fileName() == QString::fromUtf8("PLATE_2шт(2).dxf"),
            "case-insensitive duplicate must retain legacy suffix");
    const auto before = dir.entryList(QDir::Files);
    QFile old(exported.files[0]);
    require(old.open(QIODevice::ReadOnly), "read existing DXF");
    const auto bytes = old.readAll();
    old.close();
    auto fresh = part;
    fresh.name = "fresh";
    exported = export_parts(dir.path(), {fresh, upper});
    require(!exported.error.isEmpty() && exported.files.isEmpty(),
            "existing destination must reject batch without renaming CAM identifiers");
    require(dir.entryList(QDir::Files) == before, "collision preflight created files");
    require(old.open(QIODevice::ReadOnly) && old.readAll() == bytes, "existing file overwritten");
    old.close();
    auto bad = part;
    bad.shape = Washer{60, 70};
    exported = export_parts(dir.path(), {fresh, bad});
    require(!exported.error.isEmpty() && exported.files.isEmpty(),
            "preflight validation must reject entire batch");
    require(dir.entryList(QDir::Files) == before, "invalid batch created files");
    part.name = "../../escape";
    exported = export_parts(dir.path(), {fresh, part});
    require(!exported.error.isEmpty() && exported.files.isEmpty(), "unsafe name accepted");
    require(dir.entryList(QDir::Files) == before, "unsafe batch created files");
    QTemporaryDir blocked;
    const QDir blocked_dir(blocked.path());
    require(blocked_dir.mkdir("plate_2шт(1).dxf"), "seed conflicting directory");
    part = {"plate", Rectangle{}, 2, {}};
    exported = export_parts(blocked.path(), {part, part});
    require(!exported.error.isEmpty() && exported.files.isEmpty(),
            "existing directory at duplicate path must reject entire batch");
    require(blocked_dir.entryList(QDir::Files).isEmpty(), "late collision created first DXF");
    QTemporaryDir long_names;
    part.name = std::string(247, 'a');
    exported = export_parts(long_names.path(), {part, part});
    require(!exported.error.isEmpty() && exported.files.isEmpty(),
            "overlong duplicate suffix must reject entire batch");
    require(QDir(long_names.path()).isEmpty(), "long-name preflight created files");
    QTemporaryDir auto_names;
    part = {"", Washer{100.5, 50.25}, 8, 2.5};
    exported = export_parts(auto_names.path(), {part, Part{"", Circle{150}, 3, 8.0}});
    require(exported.error.isEmpty() && exported.files.size() == 2, "automatic name export");
    require(QFileInfo(exported.files[0]).fileName() == "Ш_100,5_50,25_2,5мм_8шт.dxf",
            "automatic name changed on disk");
    require(QFileInfo(exported.files[1]).fileName() == "К_150_8мм_3шт.dxf",
            "circle filename must not contain a second size on disk");
    QFile saved(exported.files[0]);
    require(saved.open(QIODevice::ReadOnly), "read automatic DXF");
    const auto dxf = saved.readAll();
    require(dxf.contains("50.25\n") && !dxf.contains("50,25\n"),
            "CAM decimal separator must not leak into DXF geometry");
    require(!export_parts(dir.filePath("missing"), {part}).error.isEmpty(),
            "nonexistent output directory");
    require(!export_parts(dir.path(), {}).error.isEmpty(), "empty batch");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        check_names();
        check_export();
        std::cout << "CAM filenames, locale independence and export safety checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
