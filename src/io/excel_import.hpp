#pragma once

#include "dxfgen/part.hpp"
#include <QStringList>
#include <array>

namespace dxfgen {
struct InputRow {
    int source_row = 0; // Excel row, 1-based; 0 for a manual part.
    std::array<QString, 6> cells;
    // Keep untouched Excel cell errors and full-precision numbers when another
    // cell is edited. Replacing a cell clears only that cell's source metadata.
    std::array<QString, 6> cell_errors;
    std::array<std::optional<double>, 6> numeric_values{};
    QString source_error; // Structural row error; cannot be repaired by editing one cell.
    std::optional<Part> part;
    QStringList parse_errors;
};

struct ImportResult {
    std::vector<InputRow> rows;
    QString sheet_name;
    QString error; // File/structure error: do not replace the currently displayed list.
    QString source_description;
    bool needs_text_options = false;
};

[[nodiscard]] QString canonical_shape_type(const QString& text);
[[nodiscard]] bool valid_import_header(const std::array<QString, 6>& cells);
[[nodiscard]] QStringList row_errors(const InputRow& row);
void parse_row(InputRow& row);
[[nodiscard]] ImportResult import_xlsx(const QString& path);
} // namespace dxfgen
