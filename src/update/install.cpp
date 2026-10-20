#include "update/install.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <cstdio>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace dxfgen::update {
namespace {
const QString prefix = QStringLiteral(".dxfgen-update-");
const QByteArray owner = "dxf-generator portable updater v1\n";
const QStringList files = {"owner",      "manifest.json",  "helper.exe",  "payload.exe",
                           "backup.exe", "preparing.lock", "helper.lock", "ready",
                           "healthy",    "abort",          "error.txt"};
QString path(const Transaction& t, const QString& name) { return QDir(t.directory).filePath(name); }
bool linked(const QString& name) {
#ifdef Q_OS_WIN
    const auto wide = QDir::toNativeSeparators(name).toStdWString();
    const auto flags = GetFileAttributesW(wide.c_str());
    if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT))
        return true;
#endif
    return QFileInfo(name).isSymLink();
}
QByteArray read_small(const QString& file, qint64 limit = 16 * 1024) {
    QFile input(file);
    if (linked(file) || !input.open(QIODevice::ReadOnly) || input.size() > limit)
        return {};
    return input.readAll();
}
QByteArray hash_file(const QString& file) {
    QFile input(file);
    if (linked(file) || !input.open(QIODevice::ReadOnly) || input.size() > max_payload_size)
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input))
        return {};
    return hash.result().toHex();
}
bool write_new(const QString& name, const QByteArray& bytes) {
    QFile file(name);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
           file.write(bytes) == bytes.size() && file.flush();
}
bool safe_workspace(const QString& directory) {
    const QFileInfo root(directory);
    if (!root.isDir() || linked(directory) || !root.fileName().startsWith(prefix) ||
        read_small(QDir(directory).filePath("owner"), 128) != owner)
        return false;
    const QDir dir(directory);
    for (const auto& file :
         dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        if (!files.contains(file.fileName()) || !file.isFile() || linked(file.absoluteFilePath()))
            return false;
    return true;
}
bool remove_files(const QString& directory) {
    const QDir dir(directory);
    // Keep ownership/manifest until everything else is gone, so an interrupted
    // cleanup remains identifiable. Never recurse or touch unexpected files.
    for (const auto& name : {"payload.exe", "error.txt", "ready", "healthy", "abort", "helper.exe",
                             "backup.exe", "manifest.json", "owner"}) {
        const auto file = dir.filePath(QLatin1String(name));
        if (QFileInfo::exists(file) && !QFile::remove(file))
            return false;
    }
    return true;
}
bool replace_file(const QString& source, const QString& destination) {
#ifdef Q_OS_WIN
    const auto from = QDir::toNativeSeparators(source).toStdWString();
    const auto to = QDir::toNativeSeparators(destination).toStdWString();
    return MoveFileExW(from.c_str(), to.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(QFile::encodeName(source).constData(),
                       QFile::encodeName(destination).constData()) == 0;
#endif
}
bool same_path(const QString& a, const QString& b) {
#ifdef Q_OS_WIN
    return QDir::cleanPath(QDir::fromNativeSeparators(a))
               .compare(QDir::cleanPath(QDir::fromNativeSeparators(b)), Qt::CaseInsensitive) == 0;
#else
    return QDir::cleanPath(a) == QDir::cleanPath(b);
#endif
}
bool valid_transaction(const Transaction& t) {
    const auto loaded = read_transaction(t.directory, t.token);
    return loaded && same_path(loaded->target, t.target) && loaded->version == t.version &&
           loaded->old_sha256 == t.old_sha256 && loaded->new_sha256 == t.new_sha256 &&
           loaded->size == t.size;
}
[[maybe_unused]] void record_error(const Transaction& t, const QString& message) {
    write_new(path(t, "error.txt"), message.left(2000).toUtf8());
}
} // namespace

std::optional<Transaction> read_transaction(const QString& directory, const QString& token) {
    if (!safe_workspace(directory))
        return {};
    const auto document =
        QJsonDocument::fromJson(read_small(QDir(directory).filePath("manifest.json")));
    if (!document.isObject())
        return {};
    const auto data = document.object();
    const auto name = data.value("target").toString();
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
    static const QRegularExpression nonce(QStringLiteral("^[0-9a-f]{32}$"));
    static const QRegularExpression forbidden(QStringLiteral("[<>:\"/\\\\|?*\\x{0000}-\\x{001f}]"));
    Transaction result;
    result.directory = QFileInfo(directory).absoluteFilePath();
    result.token = data.value("token").toString();
    result.version = data.value("version").toString();
    result.old_sha256 = data.value("old_sha256").toString().toLatin1();
    result.new_sha256 = data.value("new_sha256").toString().toLatin1();
    const double size = data.value("size").toDouble(-1);
    if (data.value("schema").toInt() != 1 || !nonce.match(result.token).hasMatch() ||
        (!token.isEmpty() && token != result.token) || !version(result.version) ||
        !hash.match(QString::fromLatin1(result.old_sha256)).hasMatch() ||
        !hash.match(QString::fromLatin1(result.new_sha256)).hasMatch() || name.isEmpty() ||
        name == "." || name == ".." || forbidden.match(name).hasMatch() ||
        !name.endsWith(".exe", Qt::CaseInsensitive) || size < 256 || size > max_payload_size ||
        size != std::floor(size))
        return {};
    result.size = static_cast<qint64>(size);
    result.target = QFileInfo(directory).dir().filePath(name);
    if (linked(result.target))
        return {};
    return result;
}
PreparedUpdate prepare_update(const QString& executable, const Release& release,
                              const QByteArray& bytes) {
    if (const auto error = verify_payload(bytes, release); !error.isEmpty())
        return {{}, {}, error};
    const QFileInfo target(executable);
    if (!target.isFile() || linked(executable) ||
        !target.fileName().endsWith(".exe", Qt::CaseInsensitive))
        return {{},
                {},
                QStringLiteral(
                    "Для обновления нужен обычный Windows EXE в доступной для записи папке.")};
    QTemporaryDir folder(target.dir().filePath(prefix + "XXXXXX"));
    if (!folder.isValid())
        return {
            {},
            {},
            QStringLiteral(
                "Не удалось создать временную папку рядом с программой. Проверьте права записи.")};
    folder.setAutoRemove(false);
    const auto dir = folder.path();
    auto lock = std::make_shared<QLockFile>(QDir(dir).filePath("preparing.lock"));
    lock->setStaleLockTime(0);
    Transaction t{dir,
                  QUuid::createUuid().toString(QUuid::Id128),
                  target.absoluteFilePath(),
                  release.version,
                  hash_file(executable),
                  release.sha256,
                  release.size};
    const auto fail = [&](const QString& error) -> PreparedUpdate {
        remove_files(dir);
        lock->unlock();
        QDir().rmdir(dir);
        return {{}, {}, error};
    };
    if (!lock->tryLock() || !write_new(path(t, "owner"), owner) || t.old_sha256.isEmpty() ||
        t.old_sha256 == t.new_sha256)
        return fail(QStringLiteral("Не удалось подготовить обновление."));
    QJsonObject manifest{{"schema", 1},
                         {"token", t.token},
                         {"target", target.fileName()},
                         {"version", t.version},
                         {"old_sha256", QString::fromLatin1(t.old_sha256)},
                         {"new_sha256", QString::fromLatin1(t.new_sha256)},
                         {"size", t.size}};
    if (!write_new(path(t, "manifest.json"),
                   QJsonDocument(manifest).toJson(QJsonDocument::Compact)) ||
        !write_new(path(t, "payload.exe"), bytes) ||
        !QFile::copy(executable, path(t, "helper.exe")))
        return fail(QStringLiteral(
            "Не удалось сохранить файлы обновления. Текущая программа не изменена."));
    if (!valid_transaction(t))
        return fail(QStringLiteral("Не удалось проверить подготовленное обновление."));
    return {t, std::move(lock), {}};
}
QString install_files(const Transaction& t) {
    if (!valid_transaction(t) || hash_file(t.target) != t.old_sha256 ||
        hash_file(path(t, "payload.exe")) != t.new_sha256 ||
        QFileInfo(path(t, "payload.exe")).size() != t.size)
        return QStringLiteral("Файлы изменились после проверки. Замена отменена.");
    if (QFileInfo::exists(path(t, "backup.exe")) || !QFile::copy(t.target, path(t, "backup.exe")) ||
        hash_file(path(t, "backup.exe")) != t.old_sha256)
        return QStringLiteral("Не удалось создать резервную копию. Текущая программа не изменена.");
    if (!replace_file(path(t, "payload.exe"), t.target))
        return QStringLiteral("Не удалось заменить EXE. Закройте другие экземпляры программы и "
                              "проверьте права записи.");
    return {};
}
QString restore_files(const Transaction& t) {
    if (!valid_transaction(t) || hash_file(path(t, "backup.exe")) != t.old_sha256)
        return QStringLiteral("Не удалось проверить резервную копию.");
    const auto current = hash_file(t.target);
    if (current != t.new_sha256 && current != t.old_sha256 && QFileInfo::exists(t.target))
        return QStringLiteral(
            "Файл программы изменён. Резервная копия сохранена для восстановления.");
    if (current == t.old_sha256)
        return {};
    return replace_file(path(t, "backup.exe"), t.target)
               ? QString{}
               : QStringLiteral("Не удалось восстановить EXE. Резервная копия сохранена.");
}
bool cleanup_workspace(const QString& directory, const QString& executable,
                       const QString& current_version) {
    if (!safe_workspace(directory) ||
        !same_path(QFileInfo(directory).dir().absolutePath(), QFileInfo(executable).absolutePath()))
        return false;
    QLockFile preparing(QDir(directory).filePath("preparing.lock")),
        helper(QDir(directory).filePath("helper.lock"));
    preparing.setStaleLockTime(0);
    helper.setStaleLockTime(0);
    if (!preparing.tryLock() || !helper.tryLock())
        return false;
    const auto t = read_transaction(directory);
    if (t) {
        if (!same_path(t->target, QFileInfo(executable).absoluteFilePath()))
            return false;
        const auto current = hash_file(executable);
        if (current != t->old_sha256 &&
            !(current == t->new_sha256 && current_version == t->version &&
              read_small(path(*t, "healthy"), 128) == t->token.toLatin1()))
            return false;
    } else if (QFileInfo::exists(QDir(directory).filePath("backup.exe"))) {
        return false; // An unidentified backup must never be discarded.
    }
    if (!remove_files(directory))
        return false;
    helper.unlock();
    preparing.unlock();
    return QDir().rmdir(directory);
}
Maintenance maintain_updates(const QString& executable, const QString& current_version) {
    Maintenance result;
    const QDir parent = QFileInfo(executable).dir();
    const auto workspaces =
        parent.entryInfoList({prefix + '*'}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    QByteArray current_hash;
    for (const auto& folder : workspaces) {
        const auto directory = folder.absoluteFilePath();
        if (!safe_workspace(directory))
            continue;
        const auto t = read_transaction(directory);
        if (t && !same_path(t->target, QFileInfo(executable).absoluteFilePath()))
            continue;
        if (t) {
            if (current_hash.isEmpty())
                current_hash = hash_file(executable);
            if (current_hash == t->new_sha256) {
                if (current_version != t->version) {
                    result.wrong_version = true;
                    result.messages << QStringLiteral(
                        "Версия скачанного EXE не совпала с релизом. Резервная копия сохранена.");
                    continue;
                }
                if (read_small(path(*t, "healthy"), 128) != t->token.toLatin1()) {
                    QFile::remove(path(*t, "healthy"));
                    write_new(path(*t, "healthy"), t->token.toLatin1());
                }
            }
            const auto message = QString::fromUtf8(read_small(path(*t, "error.txt"), 2048));
            if (!message.isEmpty())
                result.messages << message;
        }
        if (!cleanup_workspace(directory, executable, current_version))
            result.pending = true;
    }
    return result;
}
QString launch_helper(const PreparedUpdate& prepared, qint64 parent_pid) {
#ifdef Q_OS_WIN
    if (!prepared.transaction || !prepared.lock || !prepared.lock->isLocked())
        return QStringLiteral("Обновление не подготовлено.");
    const auto& t = *prepared.transaction;
    if (!valid_transaction(t) || hash_file(path(t, "helper.exe")) != t.old_sha256)
        return QStringLiteral("Не удалось проверить помощник обновления.");
    if (!QProcess::startDetached(
            path(t, "helper.exe"),
            {"--apply-update", t.directory, t.token, QString::number(parent_pid)}, t.directory))
        return QStringLiteral("Не удалось запустить помощник обновления.");
    return {};
#else
    Q_UNUSED(prepared);
    Q_UNUSED(parent_pid);
    return QStringLiteral("Автоматическая замена EXE доступна в Windows x64.");
#endif
}
void schedule_cleanup() {
    auto* timer = new QTimer(QCoreApplication::instance());
    QObject::connect(timer, &QTimer::timeout, timer, [timer, attempts = 0]() mutable {
        const auto result = maintain_updates(QCoreApplication::applicationFilePath(),
                                             QCoreApplication::applicationVersion());
        if (!result.pending || ++attempts >= 60) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(500);
}
void discard_prepared(PreparedUpdate& prepared) {
    if (!prepared.transaction)
        return;
    const auto t = *prepared.transaction;
    if (valid_transaction(t))
        write_new(path(t, "abort"), t.token.toLatin1());
    prepared.lock.reset();
    cleanup_workspace(t.directory, t.target, QCoreApplication::applicationVersion());
    schedule_cleanup();
    prepared.transaction.reset();
}
int run_helper(const QStringList& arguments) {
#ifdef Q_OS_WIN
    if (arguments.size() != 5 || arguments[1] != "--apply-update")
        return 2;
    const auto transaction = read_transaction(arguments[2], arguments[3]);
    if (!transaction ||
        !same_path(QCoreApplication::applicationFilePath(), path(*transaction, "helper.exe")) ||
        hash_file(QCoreApplication::applicationFilePath()) != transaction->old_sha256)
        return 2;
    const auto& t = *transaction;
    bool pid_ok = false;
    const auto pid = arguments[4].toUInt(&pid_ok);
    if (!pid_ok || pid == GetCurrentProcessId())
        return 2;
    HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!parent) {
        record_error(t, QStringLiteral("Не удалось дождаться закрытия программы."));
        return 2;
    }
    wchar_t image[32768];
    DWORD image_size = 32768;
    if (!QueryFullProcessImageNameW(parent, 0, image, &image_size) ||
        !same_path(QString::fromWCharArray(image, static_cast<int>(image_size)), t.target)) {
        CloseHandle(parent);
        return 2;
    }
    QLockFile lock(path(t, "helper.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock() || !write_new(path(t, "ready"), t.token.toLatin1())) {
        CloseHandle(parent);
        return 2;
    }
    DWORD wait = WAIT_TIMEOUT;
    QElapsedTimer parent_timer;
    parent_timer.start();
    while (parent_timer.elapsed() < 60000) {
        if (read_small(path(t, "abort"), 128) == t.token.toLatin1()) {
            CloseHandle(parent);
            return 2;
        }
        wait = WaitForSingleObject(parent, 100);
        if (wait != WAIT_TIMEOUT)
            break;
    }
    CloseHandle(parent);
    if (wait != WAIT_OBJECT_0) {
        record_error(t, QStringLiteral("Программа не закрылась. Обновление отменено."));
        return 2;
    }
    QString error = install_files(t);
    if (error.isEmpty()) {
        // Obtain the process handle at creation: reopening by PID can miss a
        // new executable that fails immediately, exactly when rollback is needed.
        const auto executable = QDir::toNativeSeparators(t.target).toStdWString();
        const auto working =
            QDir::toNativeSeparators(QFileInfo(t.target).absolutePath()).toStdWString();
        auto command = L"\"" + executable + L"\" --complete-update " + t.token.toStdWString();
        STARTUPINFOW startup{};
        startup.cb = static_cast<DWORD>(sizeof(startup));
        PROCESS_INFORMATION process{};
        if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                           CREATE_UNICODE_ENVIRONMENT, nullptr, working.c_str(), &startup,
                           &process)) {
            CloseHandle(process.hThread);
            HANDLE child = process.hProcess;
            QElapsedTimer timer;
            timer.start();
            bool stopped = false;
            while (timer.elapsed() < 30000) {
                if (read_small(path(t, "healthy"), 128) == t.token.toLatin1()) {
                    if (child)
                        CloseHandle(child);
                    return 0; // New process cleans this directory after our exit.
                }
                if (child && WaitForSingleObject(child, 100) == WAIT_OBJECT_0) {
                    stopped = true;
                    break;
                }
            }
            if (child)
                CloseHandle(child);
            if (!stopped) {
                record_error(t,
                             QStringLiteral(
                                 "Новая версия не подтвердила запуск. Резервная копия сохранена."));
                return 3; // Never terminate a running application to force rollback.
            }
        }
        error = QStringLiteral("Новая версия не запустилась. Восстановлена предыдущая версия.");
        if (const auto restore = restore_files(t); !restore.isEmpty()) {
            record_error(t, restore);
            MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(restore.utf16()), L"DXF Generator",
                        MB_OK | MB_ICONERROR);
            return 3;
        }
    }
    record_error(t, error);
    if (!QProcess::startDetached(t.target, {}, QFileInfo(t.target).absolutePath()))
        MessageBoxW(nullptr, L"Не удалось перезапустить программу. Запустите EXE вручную.",
                    L"DXF Generator", MB_OK | MB_ICONERROR);
    return 1;
#else
    Q_UNUSED(arguments);
    return 2;
#endif
}
} // namespace dxfgen::update
