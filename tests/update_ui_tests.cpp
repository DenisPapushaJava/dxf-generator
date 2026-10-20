#include "desktop/main_window.hpp"
#include "desktop/update_dialog.hpp"
#include "update_fixtures.hpp"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void check() {
    using namespace dxfgen;
    std::atomic_int calls{0};
    int restart_guards = 0;
    MainWindow window;
    window.show();
    QApplication::processEvents();
    QTimer::singleShot(0, &window, [&] {
        auto* help = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!help || !help->findChild<QPushButton*>("checkUpdatesButton"))
            qFatal("No manual update button in help");
        help->reject();
    });
    window.findChild<QPushButton*>("helpButton")->click();
    struct Scenario {
        int code;
        QString tag;
        QString expected;
    };
    for (const auto& scenario :
         {Scenario{404, {}, "пока нет"}, Scenario{403, {}, "ограничил"},
          Scenario{200, "v0.2.0", "Доступна версия 0.2.0"}, Scenario{200, "v0.1.0", "актуальная"},
          Scenario{0, {}, "Ошибка сети"}}) {
        calls = 0;
        bool checked = false;
        bool retried = false;
        QTimer poll;
        poll.setInterval(5);
        QObject::connect(&poll, &QTimer::timeout, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "updateDialog")
                return;
            const auto status = dialog->findChild<QLabel*>("updateStatus")->text();
            if (status.contains(scenario.expected)) {
                require(calls == (retried ? 2 : 1), "unexpected request count");
                if (scenario.code == 404 && !retried) {
                    retried = true;
                    dialog->findChild<QPushButton*>("updatePrimaryButton")->click();
                    return;
                }
                if (scenario.tag == "v0.2.0") {
                    require(dialog->findChild<QPlainTextEdit*>("releaseNotes")
                                ->toPlainText()
                                .contains("<h1>"),
                            "release notes interpreted as HTML");
                    auto* install = dialog->findChild<QPushButton*>("updatePrimaryButton");
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
                    install->click();
                    require(restart_guards == 1 && calls == 1,
                            "cancelled unsaved-data guard still downloaded an EXE");
#else
                    require(!install->isEnabled(), "Windows installer enabled on a Linux host");
#endif
                }
                checked = true;
                poll.stop();
                dialog->reject();
            }
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &window,
                         [] { qFatal("Update UI test timed out"); });
        timeout.start(5000);
        poll.start();
        show_update_dialog(
            &window,
            [&] {
                ++restart_guards;
                return false;
            },
            [&](const QUrl& url, qint64 limit, std::atomic_bool&, const update::Progress&) {
                require(url == update::latest_url() && limit == 1024 * 1024,
                        "wrong metadata request");
                ++calls;
                return update::HttpResult{
                    scenario.code,
                    scenario.code == 200 ? update_fixture::json(update_fixture::metadata(
                                               update_fixture::payload(), scenario.tag))
                                         : QByteArray{},
                    scenario.code == 0 ? QStringLiteral("Ошибка сети") : QString{}};
            });
        timeout.stop();
        require(checked, "update result not displayed");
    }
    bool cancelled = false;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            dialog && dialog->objectName() == "updateDialog") {
            poll.stop();
            dialog->reject();
        }
    });
    poll.start();
    show_update_dialog(&window, {},
                       [&](const QUrl&, qint64, std::atomic_bool& cancel, const update::Progress&) {
                           while (!cancel)
                               QThread::msleep(1);
                           cancelled = true;
                           return update::HttpResult{0, {}, "cancelled"};
                       });
    require(cancelled, "closing the dialog did not cancel its worker");
}
} // namespace
int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setApplicationVersion("0.1.0");
    try {
        check();
        std::cout << "Manual update UI, errors and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
