#include "dxfgen/dxf_writer.hpp"
#include "dxfgen/geometry.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};
void check() {
    using namespace dxfgen;
    Part rectangle{"plate", Rectangle{120.5, 80.25}, 7, 2.0};
    const auto drawing = make_drawing(rectangle);
    require(drawing.polylines.size() == 1 && drawing.circles.empty(), "rectangle topology");
    const auto& line = drawing.polylines.front();
    require(line.closed && line.vertices.size() == 4,
            "rectangle must close once, without repeated vertex");
    require(line.vertices[2].x == 120.5 && line.vertices[2].y == 80.25, "rectangle dimensions");
    Part circle{"disc", Circle{20.5}, 1, {}};
    const auto disc = make_drawing(circle);
    require(disc.circles.size() == 1 && disc.circles[0].radius == 10.25,
            "exact circle, no tessellation");
    Part washer{"ring", Washer{100, 50}, 8, 6.0};
    const auto ring = make_drawing(washer);
    require(ring.circles.size() == 2 && ring.polylines.empty(), "washer has exactly two contours");
    require(ring.circles[0].center.x == ring.circles[1].center.x &&
                ring.circles[0].center.y == ring.circles[1].center.y,
            "concentric washer");
    require(ring.circles[0].radius == 50 && ring.circles[1].radius == 25, "washer radii");
    for (double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN(), 1'000'001.0}) {
        circle.shape = Circle{bad};
        require(!validate(circle).empty(), "invalid numeric dimension accepted");
        bool threw = false;
        try {
            (void)write_dxf(circle);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw, "export must reject invalid geometry");
    }
    for (const auto shape : {Washer{60, 70}, Washer{60, 60}, Washer{60, 0}}) {
        washer.shape = shape;
        require(!validate(washer).empty(), "invalid hole accepted");
    }
    circle = {"disc", Circle{20.5}, 0, {}};
    require(!validate(circle).empty(), "zero quantity accepted");
    circle.quantity = 1;
    circle.thickness = -3;
    require(!validate(circle).empty(), "negative metadata thickness accepted");
    circle.thickness.reset();
    circle.name = " \t";
    require(!validate(circle).empty(), "blank name accepted");
    const auto previous = std::locale();
    std::locale::global(std::locale(previous, new CommaDecimal));
    const auto dxf = write_dxf(rectangle);
    std::locale::global(previous);
    require(dxf.find("120.5\n") != std::string::npos && dxf.find("120,5") == std::string::npos,
            "locale corrupts DXF numbers");
    require(dxf.find("$INSUNITS\n70\n4\n") != std::string::npos, "DXF must declare millimetres");
    require(dxf.find("LWPOLYLINE") != std::string::npos &&
                dxf.find("DIMENSION") == std::string::npos,
            "DXF entities");
    auto same = rectangle;
    same.quantity = 999;
    same.thickness = 9;
    require(write_dxf(same) == dxf, "quantity/thickness must not duplicate/change contours");
}
} // namespace
int main() {
    try {
        check();
        std::cout << "Geometry, validation and DXF checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
