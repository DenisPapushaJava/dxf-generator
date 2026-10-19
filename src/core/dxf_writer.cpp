#include "dxfgen/dxf_writer.hpp"
#include "dxfgen/geometry.hpp"

#include <iomanip>
#include <locale>
#include <sstream>

namespace dxfgen {
std::string write_dxf(const Part& part) {
    const auto drawing = make_drawing(part);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17);
    const auto pair = [&out](int code, const auto& value) { out << code << '\n' << value << '\n'; };
    pair(0, "SECTION");
    pair(2, "HEADER");
    pair(9, "$ACADVER");
    pair(1, "AC1015");
    pair(9, "$HANDSEED");
    pair(5, "100");
    pair(9, "$INSUNITS");
    pair(70, 4);
    pair(9, "$MEASUREMENT");
    pair(70, 1);
    pair(0, "ENDSEC");
    pair(0, "SECTION");
    pair(2, "TABLES");
    pair(0, "TABLE");
    pair(2, "LTYPE");
    pair(5, "10");
    pair(330, "0");
    pair(100, "AcDbSymbolTable");
    pair(70, 1);
    pair(0, "LTYPE");
    pair(5, "11");
    pair(330, "10");
    pair(100, "AcDbSymbolTableRecord");
    pair(100, "AcDbLinetypeTableRecord");
    pair(2, "CONTINUOUS");
    pair(70, 0);
    pair(3, "Solid line");
    pair(72, 65);
    pair(73, 0);
    pair(40, 0);
    pair(0, "ENDTAB");
    pair(0, "TABLE");
    pair(2, "LAYER");
    pair(5, "20");
    pair(330, "0");
    pair(100, "AcDbSymbolTable");
    pair(70, 2);
    for (const char* name : {"0", "CUT"}) {
        pair(0, "LAYER");
        pair(5, name[0] == '0' ? "21" : "22");
        pair(330, "20");
        pair(100, "AcDbSymbolTableRecord");
        pair(100, "AcDbLayerTableRecord");
        pair(2, name);
        pair(70, 0);
        pair(62, 7);
        pair(6, "CONTINUOUS");
    }
    pair(0, "ENDTAB");
    pair(0, "TABLE");
    pair(2, "BLOCK_RECORD");
    pair(5, "30");
    pair(330, "0");
    pair(100, "AcDbSymbolTable");
    pair(70, 2);
    for (int i = 0; i < 2; ++i) {
        pair(0, "BLOCK_RECORD");
        pair(5, i == 0 ? "31" : "32");
        pair(330, "30");
        pair(100, "AcDbSymbolTableRecord");
        pair(100, "AcDbBlockTableRecord");
        pair(2, i == 0 ? "*Model_Space" : "*Paper_Space");
    }
    pair(0, "ENDTAB");
    pair(0, "ENDSEC");
    pair(0, "SECTION");
    pair(2, "BLOCKS");
    for (int i = 0; i < 2; ++i) {
        const char* name = i == 0 ? "*Model_Space" : "*Paper_Space";
        const char* owner = i == 0 ? "31" : "32";
        pair(0, "BLOCK");
        pair(5, i == 0 ? "40" : "42");
        pair(330, owner);
        pair(100, "AcDbEntity");
        pair(8, "0");
        pair(100, "AcDbBlockBegin");
        pair(2, name);
        pair(70, 0);
        pair(10, 0);
        pair(20, 0);
        pair(30, 0);
        pair(3, name);
        pair(1, "");
        pair(0, "ENDBLK");
        pair(5, i == 0 ? "41" : "43");
        pair(330, owner);
        pair(100, "AcDbEntity");
        pair(8, "0");
        pair(100, "AcDbBlockEnd");
    }
    pair(0, "ENDSEC");
    pair(0, "SECTION");
    pair(2, "ENTITIES");
    unsigned handle = 0x50;
    const auto entity = [&](const char* type) {
        std::ostringstream hex;
        hex.imbue(std::locale::classic());
        hex << std::uppercase << std::hex << handle++;
        pair(0, type);
        pair(5, hex.str());
        pair(330, "31");
        pair(100, "AcDbEntity");
        pair(8, "CUT");
    };
    for (const auto& circle : drawing.circles) {
        entity("CIRCLE");
        pair(100, "AcDbCircle");
        pair(10, circle.center.x);
        pair(20, circle.center.y);
        pair(30, 0);
        pair(40, circle.radius);
    }
    for (const auto& polyline : drawing.polylines) {
        entity("LWPOLYLINE");
        pair(100, "AcDbPolyline");
        pair(90, polyline.vertices.size());
        pair(70, polyline.closed ? 1 : 0);
        for (const auto& point : polyline.vertices) {
            pair(10, point.x);
            pair(20, point.y);
        }
    }
    pair(0, "ENDSEC");
    pair(0, "EOF");
    return out.str();
}
} // namespace dxfgen
