#include "update/release.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtEndian>
#include <cmath>

namespace dxfgen::update {
QUrl latest_url() {
    return QUrl(QStringLiteral(
        "https://api.github.com/repos/DenisPapushaJava/dxf-generator/releases/latest"));
}
QUrl releases_url() {
    return QUrl(QStringLiteral("https://github.com/DenisPapushaJava/dxf-generator/releases"));
}
bool allowed_url(const QUrl& url) {
    const auto host = url.host().toLower();
    return url.isValid() && url.scheme() == "https" && url.port(443) == 443 &&
           url.userInfo().isEmpty() && !url.hasFragment() &&
           (host == "api.github.com" || host == "github.com" ||
            host == "release-assets.githubusercontent.com" ||
            host == "objects.githubusercontent.com");
}
std::optional<QVersionNumber> version(const QString& text) {
    static const QRegularExpression pattern(
        QStringLiteral("\\Av?(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\z"));
    const auto match = pattern.match(text);
    if (!match.hasMatch())
        return {};
    return QVersionNumber(match.captured(1).toInt(), match.captured(2).toInt(),
                          match.captured(3).toInt());
}
ReleaseResult parse_release(const QByteArray& json) {
    const auto invalid = [](const QString& detail) -> ReleaseResult {
        return {{}, QStringLiteral("Релиз пока не готов к обновлению: %1").arg(detail)};
    };
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return invalid(QStringLiteral("некорректный ответ GitHub."));
    const auto root = document.object();
    const auto parsed_version = version(root.value("tag_name").toString());
    if (!parsed_version || !root.value("draft").isBool() || root.value("draft").toBool() ||
        !root.value("prerelease").isBool() || root.value("prerelease").toBool())
        return invalid(QStringLiteral("ожидается стабильная версия vX.Y.Z."));
    Release release;
    release.version = parsed_version->toString();
    release.notes = root.value("body").toString().left(32 * 1024);
    int found = 0;
    for (const auto& entry : root.value("assets").toArray()) {
        const auto asset = entry.toObject();
        if (asset.value("name").toString() != QLatin1String(asset_name))
            continue;
        ++found;
        release.url = QUrl(asset.value("browser_download_url").toString(), QUrl::StrictMode);
        const auto expected_path =
            QStringLiteral("/DenisPapushaJava/dxf-generator/releases/download/%1/%2")
                .arg(root.value("tag_name").toString(), QLatin1String(asset_name));
        if (!allowed_url(release.url) || release.url.host() != "github.com" ||
            release.url.path() != expected_path || release.url.hasQuery() ||
            asset.value("state").toString() != "uploaded")
            return invalid(QStringLiteral("неподходящий файл Windows x64."));
        const auto digest = asset.value("digest").toString();
        static const QRegularExpression hash(QStringLiteral("^sha256:([0-9a-fA-F]{64})$"));
        const auto match = hash.match(digest);
        if (!match.hasMatch())
            return invalid(QStringLiteral("у файла нет контрольной суммы SHA-256."));
        release.sha256 = match.captured(1).toLatin1().toLower();
        const double size = asset.value("size").toDouble(-1);
        if (!std::isfinite(size) || size < 256 || size > max_payload_size ||
            std::floor(size) != size)
            return invalid(QStringLiteral("некорректный размер файла."));
        release.size = static_cast<qint64>(size);
    }
    if (found != 1)
        return invalid(QStringLiteral("нужен один файл %1.").arg(QLatin1String(asset_name)));
    return {release, {}};
}
QString verify_payload(const QByteArray& bytes, const Release& release) {
    if (bytes.size() != release.size || bytes.size() < 256 || bytes.size() > max_payload_size)
        return QStringLiteral("Размер скачанного файла не совпадает с релизом.");
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != release.sha256)
        return QStringLiteral("Контрольная сумма SHA-256 не совпала. Обновление отменено.");
    if (!bytes.startsWith("MZ"))
        return QStringLiteral("Скачанный файл не является Windows EXE.");
    const auto pe = qFromLittleEndian<quint32>(bytes.constData() + 0x3c);
    if (pe > static_cast<quint32>(bytes.size() - 26) ||
        bytes.mid(pe, 4) != QByteArray("PE\0\0", 4) ||
        qFromLittleEndian<quint16>(bytes.constData() + pe + 4) != 0x8664 ||
        qFromLittleEndian<quint16>(bytes.constData() + pe + 24) != 0x20b)
        return QStringLiteral("Ожидается Windows x64 EXE (PE32+).");
    return {};
}
} // namespace dxfgen::update
