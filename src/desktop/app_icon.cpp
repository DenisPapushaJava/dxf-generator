#include "desktop/app_icon.hpp"

#include <QPainter>
#include <QPainterPath>

namespace dxfgen {

QImage app_icon_image(int size) {
    if (size <= 0)
        return {};
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    // Snap the rectangle to whole pixels, especially at 16/20/24 px.
    const double scale = static_cast<double>(size) / 64.0;
    const int left = qRound(26 * scale);
    const int top = qRound(4 * scale);
    const int right = qRound(60 * scale);
    const int bottom = qRound(28 * scale);
    painter.setBrush(QColor("#2563eb"));
    painter.drawRect(QRect(left, top, right - left, bottom - top));

    painter.scale(scale, scale);
    painter.setBrush(QColor("#22c55e"));
    painter.drawEllipse(QPointF(48, 46), 12, 12);

    // The washer's opening stays transparent on light and dark backgrounds.
    QPainterPath washer;
    washer.setFillRule(Qt::OddEvenFill);
    washer.addEllipse(QPointF(18, 44), 14, 14);
    washer.addEllipse(QPointF(18, 44), 6, 6);
    painter.setBrush(QColor("#ef4444"));
    painter.drawPath(washer);
    return image;
}

QIcon make_app_icon() {
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 128, 256})
        icon.addPixmap(QPixmap::fromImage(app_icon_image(size)));
    return icon;
}

} // namespace dxfgen
