// Optional development tool; not compiled into the application.
// Build/run commands: resources/README.md.
#include "desktop/app_icon.hpp"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QGuiApplication>
#include <QTextStream>
#include <array>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    if (app.arguments().size() != 2) {
        QTextStream(stderr) << "Usage: generate-app-icon output.ico\n";
        return 1;
    }
    constexpr std::array sizes{16, 20, 24, 32, 40, 48, 64, 128, 256};
    std::array<QByteArray, sizes.size()> frames;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        QBuffer buffer(&frames[i]);
        buffer.open(QIODevice::WriteOnly);
        if (!dxfgen::app_icon_image(sizes[i]).save(&buffer, "PNG"))
            return 2;
    }
    QByteArray ico;
    QDataStream stream(&ico, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * static_cast<quint32>(sizes.size());
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const auto dimension = static_cast<quint8>(sizes[i] == 256 ? 0 : sizes[i]);
        stream << dimension << dimension << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << static_cast<quint32>(frames[i].size()) << offset;
        offset += static_cast<quint32>(frames[i].size());
    }
    for (const auto& frame : frames)
        stream.writeRawData(frame.constData(), static_cast<int>(frame.size()));
    QFile output(app.arguments()[1]);
    if (stream.status() != QDataStream::Ok || !output.open(QIODevice::WriteOnly) ||
        output.write(ico) != ico.size() || !output.flush()) {
        QTextStream(stderr) << "Cannot write icon: " << output.errorString() << '\n';
        return 3;
    }
    QTextStream(stdout) << "Wrote " << ico.size() << " bytes, " << sizes.size() << " sizes\n";
    return 0;
}
