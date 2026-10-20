#include "update/http.hpp"
#include "update/release.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace dxfgen::update {
namespace {
#ifdef Q_OS_WIN
struct CloseInternet {
    void operator()(void* handle) const {
        if (handle)
            WinHttpCloseHandle(handle);
    }
};
using Internet = std::unique_ptr<void, CloseInternet>;
QString header(HINTERNET request, DWORD kind) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, kind, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (!size || size > 16 * 1024)
        return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, kind, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &size,
                             WINHTTP_NO_HEADER_INDEX))
        return {};
    return QString::fromWCharArray(value.c_str());
}
HttpResult request(const QUrl& url, qint64 limit, std::atomic_bool& cancel,
                   const Progress& progress) {
    Internet session(WinHttpOpen(L"DXFGenerator-Updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session || !WinHttpSetTimeouts(session.get(), 5000, 10000, 10000, 15000))
        return {0, {}, QStringLiteral("Не удалось открыть сетевое соединение Windows.")};
    const auto host = url.host().toStdWString();
    Internet connection(
        WinHttpConnect(session.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    const auto path = (url.path(QUrl::FullyEncoded) +
                       (url.hasQuery() ? '?' + url.query(QUrl::FullyEncoded) : QString{}))
                          .toStdWString();
    Internet handle(connection
                        ? WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE)
                        : nullptr);
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!handle || !WinHttpSetOption(handle.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                                     sizeof(redirect_policy)))
        return {0, {}, QStringLiteral("Не удалось подготовить HTTPS-запрос.")};
    const auto headers =
        url.host() == "api.github.com"
            ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n"
            : L"Accept: application/octet-stream\r\n";
    if (cancel ||
        !WinHttpSendRequest(handle.get(), headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA,
                            0, 0, 0) ||
        !WinHttpReceiveResponse(handle.get(), nullptr))
        return {
            0, {}, QStringLiteral("Не удалось связаться с GitHub по HTTPS. Проверьте интернет.")};
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(handle.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &length,
                             WINHTTP_NO_HEADER_INDEX))
        return {0, {}, QStringLiteral("Не удалось прочитать ответ GitHub.")};
    HttpResult result;
    result.status = static_cast<int>(status);
    result.redirect = header(handle.get(), WINHTTP_QUERY_LOCATION);
    if (status != 200)
        return result;
    bool total_ok = false;
    const auto total = header(handle.get(), WINHTTP_QUERY_CONTENT_LENGTH).toLongLong(&total_ok);
    if (total_ok && total > limit)
        return {0, {}, QStringLiteral("Ответ GitHub превышает допустимый размер.")};
    QElapsedTimer timer;
    timer.start();
    char buffer[64 * 1024];
    while (!cancel && timer.elapsed() < 120000) {
        DWORD read = 0;
        if (!WinHttpReadData(handle.get(), buffer, static_cast<DWORD>(sizeof(buffer)), &read))
            return {0, {}, QStringLiteral("Загрузка прервалась. Текущая программа не изменена.")};
        if (!read)
            return result;
        if (result.body.size() + static_cast<qint64>(read) > limit)
            return {0, {}, QStringLiteral("Ответ GitHub превышает допустимый размер.")};
        result.body.append(buffer, static_cast<qsizetype>(read));
        if (progress)
            progress(result.body.size(), total_ok ? total : 0);
    }
    return {0,
            {},
            cancel ? QStringLiteral("Отменено.")
                   : QStringLiteral("Время ожидания загрузки истекло.")};
}
#else
// Linux is a development host. curl is not part of the Windows distribution;
// its user configuration is disabled, and redirects are checked by our code.
HttpResult request(const QUrl& url, qint64 limit, std::atomic_bool& cancel,
                   const Progress& progress) {
    const auto curl = QStandardPaths::findExecutable(QStringLiteral("curl"));
    QTemporaryDir temp;
    if (curl.isEmpty() || !temp.isValid())
        return {
            0, {}, QStringLiteral("Для проверки на Linux нужен curl и доступная временная папка.")};
    const auto headers = temp.filePath("headers"), body = temp.filePath("body");
    QStringList args = {"--disable",
                        "--silent",
                        "--show-error",
                        "--proto",
                        "=https",
                        "--connect-timeout",
                        "10",
                        "--max-time",
                        "120",
                        "--max-filesize",
                        QString::number(limit),
                        "--user-agent",
                        "DXFGenerator-Updater/1",
                        "--dump-header",
                        headers,
                        "--output",
                        body,
                        "--write-out",
                        "%{http_code}"};
    if (url.host() == "api.github.com")
        args << "--header" << "Accept: application/vnd.github+json" << "--header"
             << "X-GitHub-Api-Version: 2022-11-28";
    args << "--" << url.toString(QUrl::FullyEncoded);
    QProcess process;
    process.start(curl, args);
    if (!process.waitForStarted(3000))
        return {0, {}, QStringLiteral("Не удалось запустить curl.")};
    while (!process.waitForFinished(100)) {
        if (cancel || QFileInfo(body).size() > limit) {
            process.kill();
            process.waitForFinished(3000);
            return {0,
                    {},
                    cancel ? QStringLiteral("Отменено.")
                           : QStringLiteral("Превышен размер загрузки.")};
        }
        if (progress)
            progress(QFileInfo(body).size(), 0);
    }
    if (cancel || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {
            0,
            {},
            cancel ? QStringLiteral("Отменено.")
                   : QStringLiteral("Не удалось связаться с GitHub по HTTPS. Проверьте интернет.")};
    HttpResult result;
    result.status = process.readAllStandardOutput().trimmed().toInt();
    QFile header_file(headers);
    if (header_file.open(QIODevice::ReadOnly))
        for (const auto& line : header_file.readAll().split('\n'))
            if (line.toLower().startsWith("location:"))
                result.redirect = QString::fromUtf8(line.mid(9)).trimmed();
    QFile data(body);
    if (data.size() > limit)
        return {0, {}, QStringLiteral("Превышен размер загрузки.")};
    if (result.status == 200 && data.open(QIODevice::ReadOnly))
        result.body = data.readAll();
    return result;
}
#endif
} // namespace
HttpResult http_get(const QUrl& url, qint64 limit, std::atomic_bool& cancel,
                    const Progress& progress) {
    auto current = url;
    for (int redirects = 0; redirects <= 5; ++redirects) {
        if (cancel)
            return {0, {}, QStringLiteral("Отменено.")};
        if (!allowed_url(current))
            return {0, {}, QStringLiteral("Недопустимый адрес обновления.")};
        auto result = request(current, limit, cancel, progress);
        if (result.status != 301 && result.status != 302 && result.status != 303 &&
            result.status != 307 && result.status != 308)
            return result;
        if (result.redirect.isEmpty())
            return {0, {}, QStringLiteral("GitHub вернул пустой адрес перенаправления.")};
        current = current.resolved(QUrl(result.redirect, QUrl::StrictMode));
    }
    return {0, {}, QStringLiteral("Слишком много перенаправлений GitHub.")};
}
} // namespace dxfgen::update
