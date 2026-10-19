#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dxfgen {

struct Rectangle {
    double width = 120.0;
    double height = 80.0;
};
struct Circle {
    double diameter = 80.0;
};
struct Washer {
    double outer_diameter = 100.0;
    double inner_diameter = 50.0;
};
using Shape = std::variant<Rectangle, Circle, Washer>;

struct Part {
    // Optional Excel column A. Empty means a CAM name built from type and dimensions.
    std::string name;
    Shape shape = Rectangle{};
    int quantity = 1;
    std::optional<double> thickness;
    // Excel column D for circles: unused geometry, retained for legacy automatic filenames.
    double circle_filename_width = 0.0;
};

enum class Field { name, dimension, quantity, thickness };
struct ValidationError {
    Field field;
    std::string message;
};

// Millimetres throughout. Quantity/thickness are metadata, never duplicate contours.
[[nodiscard]] std::vector<ValidationError> validate(const Part& part);
[[nodiscard]] const char* shape_name(const Shape& shape);

} // namespace dxfgen
