#pragma once

#include "dxfgen/part.hpp"

#include <QStringList>

namespace dxfgen {
struct ExportResult {
    QStringList files;
    QString error;
};

[[nodiscard]] QString safe_file_stem(QString name);
// Validates the complete batch before creating files; existing files are never overwritten.
// A disk error can leave earlier files from the batch; files lists those completed writes.
[[nodiscard]] ExportResult export_parts(const QString& directory, const std::vector<Part>& parts);
} // namespace dxfgen
