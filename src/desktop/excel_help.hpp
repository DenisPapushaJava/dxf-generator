#pragma once

#include <functional>

class QWidget;

namespace dxfgen {
void show_excel_help(QWidget* parent, const std::function<bool()>& before_restart = {});
} // namespace dxfgen
