#pragma once

#include "dxfgen/part.hpp"
class QWidget;

namespace dxfgen {
[[nodiscard]] std::optional<Part> create_part(QWidget* parent);
}
