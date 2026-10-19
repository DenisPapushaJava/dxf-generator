#include "io/export_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void check() {
    using namespace dxfgen;
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory");
    const QDir dir(temp.path());
    Part part{"plate", Rectangle{}, 2, {}};
    QFile old(dir.filePath("PLATE_2ШТ.DXF"));
    require(old.open(QIODevice::WriteOnly), "seed existing file");
    old.write("KEEP");
    old.close();
    auto exported = export_parts(dir.path(), {part, part});
    require(exported.error.isEmpty() && exported.files.size() == 2, "batch export failed");
    require(QFileInfo(exported.files[0]).fileName() == QString::fromUtf8("plate_2шт_2.dxf"),
            "Windows case-insensitive collision");
    require(exported.files[0] != exported.files[1], "duplicates overwrite each other");
    require(old.open(QIODevice::ReadOnly) && old.readAll() == "KEEP", "existing file overwritten");
    old.close();
    const auto before = dir.entryList(QDir::Files);
    auto bad = part;
    bad.shape = Washer{60, 70};
    exported = export_parts(dir.path(), {part, bad});
    require(!exported.error.isEmpty() && exported.files.isEmpty(),
            "preflight validation must reject entire batch");
    require(dir.entryList(QDir::Files) == before, "invalid batch created files");
    require(safe_file_stem("CON") == "_CON" && safe_file_stem("aux.txt") == "_aux.txt",
            "reserved Windows devices");
    require(safe_file_stem("COM¹") == "_COM¹", "superscript Windows device name");
    require(safe_file_stem("../path:bad?* ").contains('/') == false, "unsafe path separator");
    part.name = "../../escape";
    exported = export_parts(dir.path(), {part});
    require(exported.error.isEmpty() && exported.files.size() == 1, "sanitized export");
    require(QFileInfo(exported.files[0]).absolutePath() == dir.absolutePath(),
            "export escaped selected folder");
    require(!export_parts(dir.filePath("missing"), {part}).error.isEmpty(),
            "nonexistent output directory");
    require(!export_parts(dir.path(), {}).error.isEmpty(), "empty batch");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        check();
        std::cout << "Export preflight, paths and collision checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
