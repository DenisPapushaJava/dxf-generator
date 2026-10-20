#pragma once

class QWidget;

namespace dxfgen {
class PartsModel;
// Export checkboxes share the source model; returns a source row, or -1 on close.
int show_parts_gallery(QWidget* parent, PartsModel& model, int current_row);
} // namespace dxfgen
