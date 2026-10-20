#pragma once

#include "io/excel_import.hpp"

namespace dxfgen {
enum class TextEncoding { automatic, utf8, utf16le, utf16be, windows1251 };
struct TextImportOptions {
    TextEncoding encoding = TextEncoding::automatic;
    QChar separator; // Null means detect from the six-column header.
};

// Content detection also handles cloud files without a filename extension.
[[nodiscard]] ImportResult import_list(const QString& path, TextImportOptions options = {});
} // namespace dxfgen
