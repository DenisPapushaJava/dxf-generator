#pragma once
#include "update/release.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace update_fixture {
inline QByteArray payload(char marker = 'n') {
    QByteArray data(512, '\0');
    data[0] = 'M';
    data[1] = 'Z';
    qToLittleEndian<quint32>(128, data.data() + 0x3c);
    data.replace(128, 4, QByteArray("PE\0\0", 4));
    qToLittleEndian<quint16>(0x8664, data.data() + 132);
    qToLittleEndian<quint16>(0x20b, data.data() + 152);
    data[200] = marker;
    return data;
}
inline QJsonObject metadata(const QByteArray& data = payload(), const QString& tag = "v0.2.0") {
    QJsonObject asset{
        {"name", dxfgen::update::asset_name},
        {"state", "uploaded"},
        {"size", data.size()},
        {"digest",
         "sha256:" + QString::fromLatin1(
                         QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())},
        {"browser_download_url",
         QString("https://github.com/DenisPapushaJava/dxf-generator/releases/download/%1/%2")
             .arg(tag, dxfgen::update::asset_name)}};
    return {{"tag_name", tag},
            {"draft", false},
            {"prerelease", false},
            {"body", "<h1>Это обычный текст</h1>\nИсправления и улучшения."},
            {"assets", QJsonArray{asset}}};
}
inline QByteArray json(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
} // namespace update_fixture
