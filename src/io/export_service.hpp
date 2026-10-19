#pragma once

#include "dxfgen/part.hpp"

#include <QStringList>

namespace dxfgen {
struct ExportResult {
    QStringList files;
    QString error;
};

struct FileNameResult {
    QString name;
    QString error;
};

// CAM contract: Cyrillic units, comma decimals, circle diameter only, no hidden name changes.
[[nodiscard]] FileNameResult cam_file_name(const Part& part);
// Validates the complete batch before creating files; existing files are never overwritten.
// Duplicate batch positions use (1), (2); an existing destination rejects the batch.
// A disk error can leave earlier files from the batch; files lists those completed writes.
[[nodiscard]] ExportResult export_parts(const QString& directory, const std::vector<Part>& parts);
} // namespace dxfgen
