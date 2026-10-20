// Real Windows process handoff against the actual statically linked GUI binary.
// This executable doubles as the old application and the copied update helper.
#include "update/install.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// Windows SDK Toolhelp declarations require windows.h first.
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

namespace {
using namespace dxfgen::update;
void require(bool value, const QString& message) {
    if (!value)
        throw std::runtime_error(message.toStdString());
}
QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read " + path);
    return file.readAll();
}
void put(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
            "Cannot write " + path);
}
QByteArray sha(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
bool wait_for(const std::function<bool()>& ready, int milliseconds = 15000) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
        if (ready())
            return true;
        QThread::msleep(50);
    } while (timer.elapsed() < milliseconds);
    return false;
}
bool clean(const QString& folder) {
    return QDir(folder)
        .entryList({".dxfgen-update-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
        .isEmpty();
}
int stop_owned_processes(const QString& target) {
    int count = 0;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
        do {
            HANDLE process =
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                            FALSE, entry.th32ProcessID);
            if (!process)
                continue;
            wchar_t image[32768];
            DWORD length = 32768;
            if (QueryFullProcessImageNameW(process, 0, image, &length) &&
                QDir::fromNativeSeparators(QString::fromWCharArray(image, static_cast<int>(length)))
                        .compare(target, Qt::CaseInsensitive) == 0) {
                ++count;
                TerminateProcess(process, 0); // Only our unique temporary test target.
                WaitForSingleObject(process, 5000);
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return count;
}
int old_process(const QString& payload, const QString& version, bool cancel) {
    const auto bytes = read(payload);
    Release release{version, {}, {}, sha(bytes), bytes.size()};
    auto prepared = prepare_update(QCoreApplication::applicationFilePath(), release, bytes);
    require(prepared.error.isEmpty() && prepared.transaction.has_value(), prepared.error);
    const auto transaction = *prepared.transaction;
    require(launch_helper(prepared, QCoreApplication::applicationPid()).isEmpty(),
            "Cannot launch helper");
    require(
        wait_for([&] { return QFileInfo::exists(QDir(transaction.directory).filePath("ready")); }),
        "Helper did not become ready");
    if (cancel) {
        discard_prepared(prepared);
        require(wait_for([&] {
                    maintain_updates(QCoreApplication::applicationFilePath(), "0.1.0");
                    return !QFileInfo::exists(transaction.directory);
                }),
                "Cancelled helper left files");
    } else {
        prepared.lock.reset();
        prepared.transaction.reset();
    }
    return 0; // The real helper must wait for this process to exit.
}
void driver(const QString& payload, const QString& version) {
    const auto self = read(QCoreApplication::applicationFilePath());
    const auto next = read(payload);
    for (const auto& scenario :
         {QStringLiteral("success"), QStringLiteral("cancel"), QStringLiteral("rollback")}) {
        QTemporaryDir root;
        require(root.isValid(), "Cannot create test folder");
        const auto folder = root.filePath(QStringLiteral("Программа с пробелами"));
        require(QDir().mkpath(folder), "Cannot create Unicode folder");
        const auto target = folder + "/portable.exe";
        put(target, self);
        put(folder + "/input.xlsx", "Excel sentinel");
        put(folder + "/part.dxf", "DXF sentinel");
        const auto failed_startup = root.filePath("failed-startup.exe");
        if (scenario == "rollback")
            put(failed_startup, self + "\nrollback probe\n");
        QProcess old;
        old.setProgram(target);
        old.setArguments(
            {"--old", scenario == "rollback" ? failed_startup : payload, version, scenario});
        old.start();
        require(old.waitForStarted(5000) && old.waitForFinished(25000) && old.exitCode() == 0,
                "Old application stage failed: " + QString::fromUtf8(old.readAllStandardError()));
        const bool updated = scenario == "success";
        const auto expected = updated ? next : self;
        const bool finished = wait_for(
            [&] {
                QFile current(target);
                return clean(folder) && current.open(QIODevice::ReadOnly) &&
                       sha(current.readAll()) == sha(expected) &&
                       (scenario != "rollback" || QFileInfo::exists(folder + "/restored.txt"));
            },
            30000);
        const int stopped = stop_owned_processes(target);
        require(finished, scenario + ": handoff/rollback did not clean the workspace");
        require(!updated || stopped == 1, "New GUI did not remain running after acknowledgement");
        require(read(folder + "/input.xlsx") == "Excel sentinel" &&
                    read(folder + "/part.dxf") == "DXF sentinel",
                "Updater changed user files");
        auto files = QDir(folder).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        files.removeAll("restored.txt");
        require(files.size() == 3, scenario + ": unexpected files remain");
        std::cout << scenario.toStdString()
                  << ": real Windows helper, replacement and cleanup passed\n";
    }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationVersion("0.1.0");
    const auto args = app.arguments();
    try {
        if (args.value(1) == "--apply-update")
            return dxfgen::update::run_helper(args);
        if (args.value(1) == "--complete-update")
            return 73; // Deliberately fail startup to exercise the real rollback path.
        if (args.size() == 5 && args[1] == "--old")
            return old_process(args[2], args[3], args[4] == "cancel");
        if (args.size() == 4 && args[1] == "--driver") {
            driver(args[2], args[3]);
            return 0;
        }
        // Restart of the old application after rollback: acknowledge recovery and clean.
        const auto folder = QFileInfo(app.applicationFilePath()).absolutePath();
        require(wait_for([&] {
                    maintain_updates(app.applicationFilePath(), "0.1.0");
                    return clean(folder);
                }),
                "Rollback cleanup failed");
        put(folder + "/restored.txt", "old version restarted");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
