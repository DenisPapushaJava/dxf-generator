#include "dxfgen/part.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace dxfgen {
namespace {
bool positive(double value) { return std::isfinite(value) && value > 0.0 && value <= 1'000'000.0; }
} // namespace

std::vector<ValidationError> validate(const Part& part) {
    std::vector<ValidationError> errors;
    if (!part.name.empty() && std::all_of(part.name.begin(), part.name.end(), [](unsigned char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        })) {
        errors.push_back({Field::name, "Название не должно состоять только из пробелов"});
    }
    if (part.quantity < 1 || part.quantity > 1'000'000)
        errors.push_back({Field::quantity, "Количество: от 1 до 1 000 000"});
    if (part.thickness && !positive(*part.thickness))
        errors.push_back(
            {Field::thickness, "Толщина должна быть больше нуля и не больше 1 000 000 мм"});
    std::visit(
        [&errors](const auto& shape) {
            using T = std::decay_t<decltype(shape)>;
            bool valid = false;
            if constexpr (std::is_same_v<T, Rectangle>)
                valid = positive(shape.width) && positive(shape.height);
            else if constexpr (std::is_same_v<T, Circle>)
                valid = positive(shape.diameter);
            else
                valid = positive(shape.outer_diameter) && positive(shape.inner_diameter) &&
                        shape.inner_diameter < shape.outer_diameter;
            if (!valid)
                errors.push_back(
                    {Field::dimension,
                     "Размеры: больше нуля, до 1 000 000 мм; отверстие меньше наружного диаметра"});
        },
        part.shape);
    return errors;
}

const char* shape_name(const Shape& shape) {
    if (std::holds_alternative<Rectangle>(shape))
        return "Прямоугольник";
    if (std::holds_alternative<Circle>(shape))
        return "Круг";
    return "Шайба";
}
} // namespace dxfgen
