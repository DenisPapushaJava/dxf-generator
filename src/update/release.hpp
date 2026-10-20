#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVersionNumber>
#include <optional>

namespace dxfgen::update {
inline constexpr qint64 max_payload_size = 64 * 1024 * 1024;
inline constexpr auto asset_name = "dxf-generator-windows-x64.exe";
QUrl latest_url();
QUrl releases_url();
bool allowed_url(const QUrl& url);
std::optional<QVersionNumber> version(const QString& text);
struct Release {
    QString version;
    QString notes;
    QUrl url;
    QByteArray sha256;
    qint64 size = 0;
};
struct ReleaseResult {
    std::optional<Release> release;
    QString error;
};
ReleaseResult parse_release(const QByteArray& json);
QString verify_payload(const QByteArray& bytes, const Release& release);
} // namespace dxfgen::update
