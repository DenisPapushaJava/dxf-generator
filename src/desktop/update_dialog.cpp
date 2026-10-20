#include "desktop/update_dialog.hpp"
#include "update/install.hpp"
#include "update/release.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace dxfgen {
namespace {
constexpr bool can_install() {
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    return true;
#else
    return false;
#endif
}
class UpdateDialog final : public QDialog {
  public:
    UpdateDialog(QWidget* parent, std::function<bool()> before_restart, update::HttpGet get)
        : QDialog(parent), before_restart_(std::move(before_restart)), get_(std::move(get)) {
        setObjectName(QStringLiteral("updateDialog"));
        setWindowTitle(QStringLiteral("Обновление DXF Generator"));
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        resize(540, 340);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(12);
        layout->addWidget(new QLabel(
            QStringLiteral("Установлена версия %1").arg(QCoreApplication::applicationVersion())));
        status_ = new QLabel(QStringLiteral("Проверяем GitHub Releases…"));
        status_->setObjectName(QStringLiteral("updateStatus"));
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        notes_ = new QPlainTextEdit;
        notes_->setObjectName(QStringLiteral("releaseNotes"));
        notes_->setReadOnly(true);
        notes_->setMaximumHeight(180);
        notes_->hide();
        layout->addWidget(notes_);
        progress_ = new QProgressBar;
        progress_->setRange(0, 0);
        layout->addWidget(progress_);
        layout->addStretch();
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        close_ = buttons->button(QDialogButtonBox::Close);
        close_->setText(QStringLiteral("Отмена"));
        connect(buttons, &QDialogButtonBox::rejected, this, &UpdateDialog::reject);
        primary_ = buttons->addButton(QStringLiteral("Проверка…"), QDialogButtonBox::ActionRole);
        primary_->setObjectName(QStringLiteral("updatePrimaryButton"));
        primary_->setEnabled(false);
        primary_->setAutoDefault(false);
        auto* releases =
            buttons->addButton(QStringLiteral("Открыть релизы"), QDialogButtonBox::ActionRole);
        releases->setObjectName(QStringLiteral("openReleasesButton"));
        releases->setAutoDefault(false);
        connect(releases, &QPushButton::clicked, this,
                [] { QDesktopServices::openUrl(update::releases_url()); });
        connect(primary_, &QPushButton::clicked, this, [this] {
            if (release_ && can_install()) {
                if (before_restart_ && !before_restart_())
                    return;
                start(true);
            } else
                start(false);
        });
        layout->addWidget(buttons);
        QTimer::singleShot(0, this, [this] { start(false); });
    }
    void reject() override {
        if (handoff_)
            return;
        if (thread_) {
            cancel_ = true;
            status_->setText(QStringLiteral("Отменяем запрос…"));
            return;
        }
        update::discard_prepared(prepared_);
        QDialog::reject();
    }
    ~UpdateDialog() override {
        cancel_ = true;
        if (thread_) {
            thread_->wait();
            delete thread_;
        }
        if (!handoff_)
            update::discard_prepared(prepared_);
    }

  private:
    struct Result {
        update::HttpResult response;
        update::PreparedUpdate prepared;
    };
    void start(bool download) {
        if (thread_ || handoff_)
            return;
        cancel_ = false;
        primary_->setEnabled(false);
        close_->setText(QStringLiteral("Отмена"));
        progress_->setRange(0, 0);
        progress_->show();
        status_->setText(download ? QStringLiteral("Скачиваем и проверяем новую версию…")
                                  : QStringLiteral("Проверяем GitHub Releases…"));
        const auto url = download ? release_->url : update::latest_url();
        const auto limit = download ? update::max_payload_size : 1024 * 1024;
        const auto executable = QCoreApplication::applicationFilePath();
        const auto release = release_;
        auto result = std::make_shared<Result>();
        thread_ = QThread::create([this, download, url, limit, executable, release, result] {
            result->response = get_(url, limit, cancel_, [this](qint64 bytes, qint64 total) {
                QMetaObject::invokeMethod(
                    this,
                    [this, bytes, total] {
                        if (total > 0) {
                            progress_->setRange(0, 1000);
                            progress_->setValue(
                                static_cast<int>(std::min<qint64>(1000, bytes * 1000 / total)));
                        }
                    },
                    Qt::QueuedConnection);
            });
            if (download && !cancel_ && result->response.status == 200 &&
                result->response.error.isEmpty())
                result->prepared =
                    update::prepare_update(executable, *release, result->response.body);
        });
        connect(thread_, &QThread::finished, this, [this, result, download, worker = thread_] {
            thread_ = nullptr;
            worker->deleteLater();
            if (cancel_) {
                update::discard_prepared(result->prepared);
                QDialog::reject();
                return;
            }
            progress_->hide();
            close_->setText(QStringLiteral("Закрыть"));
            if (!result->response.error.isEmpty())
                return fail(result->response.error);
            if (result->response.status == 404)
                return fail(
                    download ? QStringLiteral("Файл релиза больше недоступен. Повторите проверку.")
                             : QStringLiteral("Опубликованных релизов пока нет. Текущая версия "
                                              "продолжает работать."));
            if (result->response.status == 403 || result->response.status == 429)
                return fail(QStringLiteral("GitHub временно ограничил запросы. Попробуйте позже."));
            if (result->response.status != 200)
                return fail(QStringLiteral("GitHub вернул ошибку HTTP %1. Попробуйте позже.")
                                .arg(result->response.status));
            if (download) {
                prepared_ = std::move(result->prepared);
                if (!prepared_.error.isEmpty())
                    return fail(prepared_.error);
                const auto error =
                    update::launch_helper(prepared_, QCoreApplication::applicationPid());
                if (!error.isEmpty()) {
                    update::discard_prepared(prepared_);
                    return fail(error);
                }
                handoff_ = true;
                close_->setEnabled(false);
                status_->setText(QStringLiteral("Подготовка перезапуска…"));
                handoff_timer_.start();
                auto* timer = new QTimer(this);
                connect(timer, &QTimer::timeout, this, [this, timer] { pollHelper(timer); });
                timer->start(50);
                return;
            }
            const auto parsed = update::parse_release(result->response.body);
            if (!parsed.error.isEmpty())
                return fail(parsed.error);
            const auto current = update::version(QCoreApplication::applicationVersion());
            if (!current)
                return fail(QStringLiteral("Не удалось определить текущую версию программы."));
            if (*update::version(parsed.release->version) <= *current)
                return fail(
                    QStringLiteral("Установлена актуальная версия %1.").arg(current->toString()));
            release_ = parsed.release;
            status_->setText(
                QStringLiteral("Доступна версия %1.%2")
                    .arg(release_->version,
                         can_install()
                             ? QStringLiteral(" После обновления программа перезапустится.")
                             : QStringLiteral(" Это выпуск для Windows x64. Автоустановка доступна "
                                              "в Windows.")));
            notes_->setPlainText(release_->notes.isEmpty()
                                     ? QStringLiteral("Описание изменений не указано.")
                                     : release_->notes);
            notes_->show();
            primary_->setText(QStringLiteral("Обновить и перезапустить"));
            primary_->setEnabled(can_install());
        });
        thread_->start();
    }
    void fail(const QString& message) {
        status_->setText(message);
        primary_->setText(QStringLiteral("Повторить проверку"));
        primary_->setEnabled(true);
        release_.reset();
        notes_->hide();
    }
    void pollHelper(QTimer* timer) {
        const auto& t = *prepared_.transaction;
        QFile ready(QDir(t.directory).filePath("ready"));
        if (ready.open(QIODevice::ReadOnly) && ready.read(128) == t.token.toLatin1()) {
            timer->stop();
            prepared_.lock.reset();
            prepared_.transaction.reset();
            QCoreApplication::exit(0);
            return;
        }
        QFile error(QDir(t.directory).filePath("error.txt"));
        if (handoff_timer_.elapsed() > 15000 || error.open(QIODevice::ReadOnly)) {
            timer->stop();
            const auto detail =
                error.isOpen()
                    ? QString::fromUtf8(error.read(2048))
                    : QStringLiteral(
                          "Помощник обновления не запустился. Текущая программа не изменена.");
            update::discard_prepared(prepared_);
            handoff_ = false;
            close_->setEnabled(true);
            fail(detail);
        }
    }
    std::function<bool()> before_restart_;
    update::HttpGet get_;
    std::atomic_bool cancel_{false};
    QThread* thread_ = nullptr;
    QLabel* status_;
    QPlainTextEdit* notes_;
    QProgressBar* progress_;
    QPushButton* primary_;
    QPushButton* close_;
    std::optional<update::Release> release_;
    update::PreparedUpdate prepared_;
    bool handoff_ = false;
    QElapsedTimer handoff_timer_;
};
} // namespace
void show_update_dialog(QWidget* parent, const std::function<bool()>& before_restart,
                        update::HttpGet get) {
    UpdateDialog dialog(parent, before_restart, std::move(get));
    dialog.exec();
}
} // namespace dxfgen
