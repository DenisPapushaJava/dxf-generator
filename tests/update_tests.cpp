#include "update/http.hpp"
#include "update/install.hpp"
#include "update/release.hpp"
#include "update_fixtures.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void put(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                file.write(bytes) == bytes.size(),
            "fixture write failed");
}
QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "fixture read failed");
    return file.readAll();
}
void check() {
    using namespace dxfgen::update;
    using namespace update_fixture;
    require(version("v1.2.10") && *version("1.2.10") > *version("1.2.9"),
            "versions compared lexically");
    for (const auto* invalid :
         {"1.2", "01.2.3", "v1.2.3-beta", "1.2.3.4", "1.2.3\n", "1.2.3;run", "9999999.0.0"})
        require(!version(QLatin1String(invalid)), "non-stable or malformed version accepted");
    for (const auto* invalid :
         {"http://github.com/file", "https://evil.example/file", "https://github.com:444/file",
          "https://user@github.com/file", "https://github.com/file#part", "file:///file"})
        require(!allowed_url(QUrl(QLatin1String(invalid))), "unsafe network URL accepted");
    std::atomic_bool cancel{false};
    require(!http_get(QUrl("http://example.invalid"), 1024, cancel, {}).error.isEmpty(),
            "unsafe URL reached transport");
    cancel = true;
    require(!http_get(latest_url(), 1024, cancel, {}).error.isEmpty(),
            "pre-cancelled request was started");
    const auto bytes = payload();
    const auto parsed = parse_release(json(metadata()));
    require(parsed.error.isEmpty() && parsed.release && parsed.release->version == "0.2.0",
            "valid release rejected");
    const auto release = *parsed.release;
    require(verify_payload(bytes, release).isEmpty(), "valid PE32+ rejected");
    auto changed = bytes;
    changed[210] = 'x';
    require(!verify_payload(changed, release).isEmpty(), "SHA mismatch accepted");
    changed = bytes;
    qToLittleEndian<quint16>(0x14c, changed.data() + 132);
    auto wrong_arch = *parse_release(json(metadata(changed))).release;
    require(!verify_payload(changed, wrong_arch).isEmpty(), "32-bit PE accepted as x64");
    require(!parse_release("invalid JSON").error.isEmpty(), "bad JSON accepted");
    auto object = metadata();
    object["prerelease"] = true;
    require(!parse_release(json(object)).error.isEmpty(), "prerelease accepted");
    object = metadata();
    object["draft"] = true;
    require(!parse_release(json(object)).error.isEmpty(), "draft accepted");
    auto bad_asset = metadata()["assets"].toArray()[0].toObject();
    bad_asset.remove("digest");
    object = metadata();
    object["assets"] = QJsonArray{bad_asset};
    require(!parse_release(json(object)).error.isEmpty(), "missing SHA digest accepted");
    bad_asset = metadata()["assets"].toArray()[0].toObject();
    bad_asset["browser_download_url"] =
        "https://github.com/other/repo/releases/download/v0.2.0/dxf-generator-windows-x64.exe";
    object = metadata();
    object["assets"] = QJsonArray{bad_asset};
    require(!parse_release(json(object)).error.isEmpty(), "another repository asset accepted");
    object = metadata();
    const auto asset = object["assets"].toArray()[0];
    object["assets"] = QJsonArray{asset, asset};
    require(!parse_release(json(object)).error.isEmpty(), "ambiguous assets accepted");

    QTemporaryDir temp;
    require(temp.isValid(), "no temporary directory");
    const auto target = temp.filePath("Моя программа.exe");
    const auto old = payload('o');
    put(target, old);
    put(temp.filePath("Заказ.xlsx"), "Excel sentinel");
    put(temp.filePath("Деталь.dxf"), "DXF sentinel");
    auto prepare = [&] {
        auto result = prepare_update(target, release, bytes);
        if (!result.error.isEmpty())
            throw std::runtime_error(result.error.toStdString());
        require(result.transaction.has_value(), "no prepared transaction");
        return result;
    };
    auto prepared = prepare();
    auto t = *prepared.transaction;
    require(!cleanup_workspace(t.directory, target, "0.1.0"), "preparation lock ignored");
    require(!read_transaction(t.directory, "wrong token"), "wrong helper token accepted");
    prepared.lock.reset();
    require(install_files(t).isEmpty(), "install transaction failed");
    require(read(target) == bytes && read(QDir(t.directory).filePath("backup.exe")) == old,
            "install lost old or new bytes");
    require(!cleanup_workspace(t.directory, target, "0.2.0"),
            "backup removed before health acknowledgement");
    QLockFile helper(QDir(t.directory).filePath("helper.lock"));
    helper.setStaleLockTime(0);
    require(helper.tryLock(), "helper test lock failed");
    put(QDir(t.directory).filePath("healthy"), "interrupted acknowledgement");
    require(maintain_updates(target, "0.2.0").pending &&
                read(QDir(t.directory).filePath("healthy")) == t.token.toLatin1(),
            "new app did not acknowledge startup while helper was running");
    helper.unlock();
    require(!maintain_updates(target, "0.2.0").pending && !QFileInfo::exists(t.directory),
            "successful update left temporary files");
    require(read(target) == bytes && read(temp.filePath("Заказ.xlsx")) == "Excel sentinel" &&
                read(temp.filePath("Деталь.dxf")) == "DXF sentinel",
            "cleanup touched user files");

    put(target, old);
    prepared = prepare();
    t = *prepared.transaction;
    prepared.lock.reset();
    require(install_files(t).isEmpty() && restore_files(t).isEmpty() && read(target) == old,
            "rollback did not restore exact original bytes");
    maintain_updates(target, "0.1.0");
    require(!QFileInfo::exists(t.directory), "rollback left helper or payload");

    prepared = prepare();
    t = *prepared.transaction;
    prepared.lock.reset();
    require(install_files(t).isEmpty() && QFile::remove(target),
            "missing-target rollback fixture failed");
    require(restore_files(t).isEmpty() && read(target) == old,
            "missing replacement could not be recovered from backup");
    maintain_updates(target, "0.1.0");

    prepared = prepare();
    t = *prepared.transaction;
    prepared.lock.reset();
    put(QDir(t.directory).filePath("payload.exe"), "tampered");
    require(!install_files(t).isEmpty() && read(target) == old,
            "tampered payload replaced the original");
    require(cleanup_workspace(t.directory, target, "0.1.0"), "failed staging left debris");

    prepared = prepare();
    t = *prepared.transaction;
    prepared.lock.reset();
    auto escaped = t;
    escaped.target = temp.filePath("Деталь.dxf");
    require(!install_files(escaped).isEmpty() && read(escaped.target) == "DXF sentinel",
            "transaction escaped its bound target");
    put(QDir(t.directory).filePath("manifest.json"), "interrupted manifest");
    maintain_updates(target, "0.1.0");
    require(!QFileInfo::exists(t.directory), "interrupted preparation not cleaned");

    prepared = prepare();
    t = *prepared.transaction;
    prepared.lock.reset();
    put(QDir(t.directory).filePath("user-file.xlsx"), "unrelated");
    require(!cleanup_workspace(t.directory, target, "0.1.0") &&
                read(QDir(t.directory).filePath("user-file.xlsx")) == "unrelated",
            "cleanup deleted unexpected contents");
    QFile::remove(QDir(t.directory).filePath("user-file.xlsx"));
#ifndef Q_OS_WIN
    require(QFile::link(temp.filePath("Заказ.xlsx"), QDir(t.directory).filePath("backup.exe")),
            "symlink fixture failed");
    require(!cleanup_workspace(t.directory, target, "0.1.0"), "cleanup followed a symlink");
    QFile::remove(QDir(t.directory).filePath("backup.exe"));
#endif
    require(cleanup_workspace(t.directory, target, "0.1.0"), "owned staging directory not cleaned");

    prepared = prepare();
    t = *prepared.transaction;
    prepared.lock.reset();
    require(install_files(t).isEmpty(), "install for wrong-version recovery failed");
    require(maintain_updates(target, "0.1.0").wrong_version &&
                QFileInfo::exists(QDir(t.directory).filePath("backup.exe")),
            "wrong embedded version discarded the backup");
    put(target, "external replacement");
    require(!restore_files(t).isEmpty() && !cleanup_workspace(t.directory, target, "0.2.0"),
            "rollback overwrote an unexpected target");
    put(target, bytes);
    require(restore_files(t).isEmpty(), "could not restore after recovery fixture");
    maintain_updates(target, "0.1.0");
    require(!QFileInfo::exists(t.directory), "recovery left temporary files");
    require(QDir(temp.path())
                    .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
                    .size() == 3,
            "updater left garbage next to the executable");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationVersion("0.1.0");
    try {
        if (app.arguments().contains("--live-check")) {
            std::atomic_bool cancel{false};
            const auto result =
                dxfgen::update::http_get(dxfgen::update::latest_url(), 1024 * 1024, cancel, {});
            std::cout << "GitHub HTTP " << result.status << ": " << result.error.toStdString()
                      << '\n';
            return result.error.isEmpty() && (result.status == 200 || result.status == 404) ? 0 : 1;
        }
        check();
        std::cout << "Release validation, install, rollback, locks and cleanup passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
