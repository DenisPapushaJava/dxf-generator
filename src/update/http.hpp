#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <atomic>
#include <functional>

namespace dxfgen::update {
struct HttpResult {
    int status = 0;
    QByteArray body{};
    QString error{};
    QString redirect{};
};
using Progress = std::function<void(qint64, qint64)>;
using HttpGet = std::function<HttpResult(const QUrl&, qint64, std::atomic_bool&, const Progress&)>;
HttpResult http_get(const QUrl& url, qint64 limit, std::atomic_bool& cancel,
                    const Progress& progress);
} // namespace dxfgen::update
