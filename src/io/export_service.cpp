#include "io/export_service.hpp"
#include "dxfgen/dxf_writer.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

namespace dxfgen {
QString safe_file_stem(QString name) {
    name = name.normalized(QString::NormalizationForm_C).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x{0000}-\\x{001f}]")), "_");
    name = name.left(80);
    if (!name.isEmpty() && name.at(name.size() - 1).isHighSurrogate())
        name.chop(1);
    while (name.endsWith('.') || name.endsWith(' '))
        name.chop(1);
    if (name.isEmpty())
        name = QStringLiteral("Деталь");
    const QString base = name.section('.', 0, 0).trimmed();
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³]|CONIN\\$|CONOUT\\$)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(base).hasMatch())
        name.prepend('_');
    return name;
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
    QSet<QString> occupied;
    for (const QString& name :
         dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        occupied.insert(name.normalized(QString::NormalizationForm_C).toCaseFolded());
    for (const auto& part : parts) {
        const auto errors = validate(part);
        if (!errors.empty()) {
            result.error = QString::fromStdString(part.name) + ": " +
                           QString::fromStdString(errors.front().message);
            return result;
        }
        QString stem = safe_file_stem(QString::fromStdString(part.name));
        if (part.thickness)
            stem += '_' + QString::number(*part.thickness, 'g', 12) + QStringLiteral("мм");
        stem += '_' + QString::number(part.quantity) + QStringLiteral("шт");
        QString name = stem + ".dxf";
        int suffix = 2;
        while (occupied.contains(name.toCaseFolded()))
            name = stem + '_' + QString::number(suffix++) + ".dxf";
        occupied.insert(name.toCaseFolded());
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
