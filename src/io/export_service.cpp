#include "io/export_service.hpp"
#include "dxfgen/dxf_writer.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

namespace dxfgen {
namespace {
QString cam_number(double value) {
    // .NET Framework Double's default general format used 15 significant digits.
    // QString::number is locale independent; upper-case exponent matches legacy G format.
    return QString::number(value, 'G', 15).replace('.', ',');
}
QString filename_error(const QString& name) {
    static const QRegularExpression forbidden(QStringLiteral("[<>:\"/\\\\|?*\\x{0000}-\\x{001f}]"));
    if (forbidden.match(name).hasMatch())
        return QStringLiteral("Имя DXF содержит запрещённый Windows символ: %1").arg(name);
    if (name.size() > 255)
        return QStringLiteral("Имя DXF длиннее 255 символов: %1").arg(name);
    const QString base = name.section('.', 0, 0).trimmed();
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³]|CONIN\\$|CONOUT\\$)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(base).hasMatch())
        return QStringLiteral("Имя DXF зарезервировано Windows: %1").arg(name);
    return {};
}
QString path_key(const QString& name) {
    return name.normalized(QString::NormalizationForm_C).toCaseFolded();
}
} // namespace

FileNameResult cam_file_name(const Part& part) {
    const auto errors = validate(part);
    if (!errors.empty())
        return {{}, QString::fromStdString(errors.front().message)};
    QString stem = QString::fromStdString(part.name);
    if (!stem.isEmpty() && stem.trimmed().isEmpty())
        return {{}, QStringLiteral("Название не должно состоять только из пробелов")};
    if (stem.isEmpty()) {
        if (const auto* rectangle = std::get_if<Rectangle>(&part.shape))
            stem = QStringLiteral("П_%1_%2").arg(cam_number(rectangle->width),
                                                 cam_number(rectangle->height));
        else if (const auto* circle = std::get_if<Circle>(&part.shape))
            stem = QStringLiteral("К_%1").arg(cam_number(circle->diameter));
        else {
            const auto& washer = std::get<Washer>(part.shape);
            stem = QStringLiteral("Ш_%1_%2").arg(cam_number(washer.outer_diameter),
                                                 cam_number(washer.inner_diameter));
        }
    }
    if (part.thickness)
        stem += '_' + cam_number(*part.thickness) + QStringLiteral("мм");
    stem += '_' + QString::number(part.quantity) + QStringLiteral("шт.dxf");
    const auto error = filename_error(stem);
    return error.isEmpty() ? FileNameResult{stem, {}} : FileNameResult{{}, error};
}

ExportResult export_parts(const QString& directory, const std::vector<Part>& parts) {
    ExportResult result;
    if (parts.empty()) {
        result.error = QStringLiteral("Выберите хотя бы одну деталь");
        return result;
    }
    const QDir dir(directory);
    if (!dir.exists()) {
        result.error = QStringLiteral("Папка экспорта не существует");
        return result;
    }
    struct Item {
        QString path;
        QByteArray bytes;
    };
    std::vector<Item> plan;
    QSet<QString> existing;
    QSet<QString> planned;
    for (const QString& name :
         dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        existing.insert(path_key(name));
    for (const auto& part : parts) {
        const auto filename = cam_file_name(part);
        if (!filename.error.isEmpty()) {
            result.error = QStringLiteral("Позиция %1: %2")
                               .arg(static_cast<qulonglong>(plan.size() + 1))
                               .arg(filename.error);
            return result;
        }
        const QString stem = filename.name.chopped(4);
        QString name = filename.name;
        int suffix = 1;
        while (planned.contains(path_key(name)))
            name = stem + '(' + QString::number(suffix++) + ").dxf";
        if (const auto error = filename_error(name); !error.isEmpty()) {
            result.error = error;
            return result;
        }
        if (existing.contains(path_key(name))) {
            result.error = QStringLiteral("Файл или папка уже существует: %1\n"
                                          "Выберите другую папку экспорта.")
                               .arg(dir.absoluteFilePath(name));
            return result;
        }
        planned.insert(path_key(name));
        plan.push_back({dir.absoluteFilePath(name), QByteArray::fromStdString(write_dxf(part))});
    }
    for (const auto& item : plan) {
        QFile file(item.path);
        // NewOnly also protects against a file created after preflight (no overwrite race).
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            result.error = item.path + ": " + file.errorString();
            return result;
        }
        if (file.write(item.bytes) != item.bytes.size() || !file.flush()) {
            result.error = item.path + ": " + file.errorString();
            file.remove();
            return result;
        }
        file.close();
        result.files.push_back(item.path);
    }
    return result;
}
} // namespace dxfgen
