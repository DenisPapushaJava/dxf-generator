#pragma once

#include "io/excel_import.hpp"
class QWidget;

namespace dxfgen {
std::optional<ImportResult> configure_text_import(QWidget* parent, const QString& path,
                                                  const QString& reason);
} // namespace dxfgen
