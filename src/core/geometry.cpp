#include "dxfgen/geometry.hpp"

#include <stdexcept>
#include <type_traits>

namespace dxfgen {
Drawing make_drawing(const Part& part) {
    const auto errors = validate(part);
    if (!errors.empty())
        throw std::invalid_argument(errors.front().message);
    return std::visit(
        [](const auto& shape) -> Drawing {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, Rectangle>) {
                return {{},
                        {{{{0.0, 0.0},
                           {shape.width, 0.0},
                           {shape.width, shape.height},
                           {0.0, shape.height}},
                          true}},
                        shape.width,
                        shape.height};
            } else {
                const double diameter = [&] {
                    if constexpr (std::is_same_v<T, Circle>)
                        return shape.diameter;
                    else
                        return shape.outer_diameter;
                }();
                const Point center{diameter / 2.0, diameter / 2.0};
                Drawing result{{{center, diameter / 2.0}}, {}, diameter, diameter};
                if constexpr (std::is_same_v<T, Washer>)
                    result.circles.push_back({center, shape.inner_diameter / 2.0});
                return result;
            }
        },
        part.shape);
}
} // namespace dxfgen
