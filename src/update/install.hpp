#pragma once

#include "update/release.hpp"
#include <QLockFile>
#include <QStringList>
#include <memory>

namespace dxfgen::update {
struct Transaction {
    QString directory, token, target, version;
    QByteArray old_sha256, new_sha256;
    qint64 size = 0;
};
struct PreparedUpdate {
    std::optional<Transaction> transaction;
    std::shared_ptr<QLockFile> lock;
    QString error;
};
PreparedUpdate prepare_update(const QString& executable, const Release& release,
                              const QByteArray& bytes);
std::optional<Transaction> read_transaction(const QString& directory, const QString& token = {});
QString install_files(const Transaction& transaction);
QString restore_files(const Transaction& transaction);
bool cleanup_workspace(const QString& directory, const QString& executable,
                       const QString& current_version);
struct Maintenance {
    bool pending = false;
    bool wrong_version = false;
    QStringList messages;
};
Maintenance maintain_updates(const QString& executable, const QString& current_version);
QString launch_helper(const PreparedUpdate& prepared, qint64 parent_pid);
void discard_prepared(PreparedUpdate& prepared);
void schedule_cleanup();
int run_helper(const QStringList& arguments);
} // namespace dxfgen::update
