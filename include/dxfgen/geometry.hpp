#pragma once

#include "dxfgen/part.hpp"

namespace dxfgen {

struct Point {
    double x;
    double y;
};
struct CircleContour {
    Point center;
    double radius;
};
struct Polyline {
    std::vector<Point> vertices;
    bool closed = true;
};
struct Drawing {
    std::vector<CircleContour> circles;
    std::vector<Polyline> polylines;
    double width = 0.0;
    double height = 0.0;
};

// Throws std::invalid_argument on invalid input. Origin is the lower-left of the bounds.
[[nodiscard]] Drawing make_drawing(const Part& part);

} // namespace dxfgen
