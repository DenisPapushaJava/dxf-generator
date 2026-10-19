#include "desktop/main_window.hpp"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DXF Generator"));
    QCoreApplication::setApplicationVersion(QStringLiteral(DXFGEN_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("DenisPapushaJava"));
    // Draw the small icon directly: no SVG plugin or external resource files at runtime.
    QPixmap icon(64, 64);
    icon.fill(Qt::transparent);
    {
        QPainter p(&icon);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#2563eb"), 5));
        p.setBrush(QColor("#dbeafe"));
        p.drawRoundedRect(7, 7, 50, 50, 8, 8);
        p.drawEllipse(22, 22, 20, 20);
    }
    app.setWindowIcon(QIcon(icon));
    dxfgen::MainWindow window;
    window.show();
    if (app.arguments().contains(QStringLiteral("--smoke-test")))
        QTimer::singleShot(200, &app, &QCoreApplication::quit);
    const auto screenshot = app.arguments().indexOf(QStringLiteral("--screenshot"));
    if (screenshot >= 0 && screenshot + 1 < app.arguments().size()) {
        const auto path = app.arguments().at(screenshot + 1);
        QTimer::singleShot(250, &app,
                           [&app, &window, path] { app.exit(window.grab().save(path) ? 0 : 2); });
    }
    return app.exec();
}
