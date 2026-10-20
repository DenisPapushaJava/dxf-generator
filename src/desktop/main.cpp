#include "desktop/app_icon.hpp"
#include "desktop/main_window.hpp"
#include "update/install.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc > 1 && std::strcmp(argv[1], "--apply-update") == 0) {
        QCoreApplication helper(argc, argv);
        return dxfgen::update::run_helper(helper.arguments());
    }
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DXF Generator"));
    QCoreApplication::setApplicationVersion(QStringLiteral(DXFGEN_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("DenisPapushaJava"));
    app.setWindowIcon(dxfgen::make_app_icon());
    dxfgen::MainWindow window;
    const auto open = app.arguments().indexOf(QStringLiteral("--open"));
    if (open >= 0 && open + 1 < app.arguments().size() &&
        !window.loadExcel(app.arguments().at(open + 1), false))
        return 2;
    window.show();
    // Local recovery/cleanup only. Network requests are exclusively user-triggered.
    QTimer::singleShot(app.arguments().contains(QStringLiteral("--complete-update")) ? 500 : 0,
                       &window, [&app, &window] {
                           const auto maintenance = dxfgen::update::maintain_updates(
                               app.applicationFilePath(), app.applicationVersion());
                           if (maintenance.wrong_version &&
                               app.arguments().contains(QStringLiteral("--complete-update"))) {
                               app.exit(73); // Let the waiting helper restore the previous binary.
                               return;
                           }
                           if (maintenance.pending)
                               dxfgen::update::schedule_cleanup();
                           if (!maintenance.messages.isEmpty())
                               QMessageBox::warning(&window, QStringLiteral("Обновление программы"),
                                                    maintenance.messages.join('\n'));
                       });
    if (app.arguments().contains(QStringLiteral("--smoke-test")))
        // Noninteractive startup check must not open the user's exit question.
        QTimer::singleShot(200, &app, [] { QCoreApplication::exit(0); });
    const auto screenshot = app.arguments().indexOf(QStringLiteral("--screenshot"));
    if (screenshot >= 0 && screenshot + 1 < app.arguments().size()) {
        const auto path = app.arguments().at(screenshot + 1);
        QTimer::singleShot(250, &app,
                           [&app, &window, path] { app.exit(window.grab().save(path) ? 0 : 2); });
    }
    return app.exec();
}
