#pragma once

#include "dxfgen/part.hpp"

namespace dxfgen {

// Restricted ASCII DXF AC1015: millimetres, CUT layer, CIRCLE/LWPOLYLINE only.
// Input is validated before writing. No labels, dimensions or quantity copies in geometry.
[[nodiscard]] std::string write_dxf(const Part& part);

} // namespace dxfgen
