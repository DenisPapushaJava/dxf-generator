#include "desktop/preview_widget.hpp"
#include "dxfgen/geometry.hpp"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace dxfgen {
PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 300);
    setAccessibleName(QStringLiteral("Предпросмотр контуров детали в миллиметрах"));
}
void PreviewWidget::setPart(const Part* part) {
    part_ = part ? std::optional<Part>(*part) : std::nullopt;
    update();
}
void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#f8fafc"));
    p.setPen(QColor("#e2e8f0"));
    for (int x = 20; x < width(); x += 20)
        for (int y = 20; y < height(); y += 20)
            p.drawPoint(x, y);
    p.setPen(QColor("#64748b"));
    if (!part_ || !validate(*part_).empty()) {
        p.drawText(rect().adjusted(30, 30, -30, -30), Qt::AlignCenter | Qt::TextWordWrap,
                   part_ ? QStringLiteral("Исправьте размеры, чтобы увидеть контур")
                         : QStringLiteral("Добавьте или выберите деталь"));
        return;
    }
    const auto drawing = make_drawing(*part_);
    const QRectF area = QRectF(rect()).adjusted(54, 30, -54, -100);
    const double scale = std::min(area.width() / drawing.width, area.height() / drawing.height);
    const double left = area.center().x() - drawing.width * scale / 2.0;
    const double top = area.center().y() - drawing.height * scale / 2.0;
    const auto point = [&](Point value) {
        return QPointF(left + value.x * scale, top + (drawing.height - value.y) * scale);
    };
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (const auto& circle : drawing.circles)
        path.addEllipse(point(circle.center), circle.radius * scale, circle.radius * scale);
    for (const auto& polyline : drawing.polylines) {
        if (polyline.vertices.empty())
            continue;
        path.moveTo(point(polyline.vertices.front()));
        for (std::size_t i = 1; i < polyline.vertices.size(); ++i)
            path.lineTo(point(polyline.vertices[i]));
        if (polyline.closed)
            path.closeSubpath();
    }
    p.setPen(QPen(QColor("#2563eb"), 2.0));
    p.setBrush(QColor("#dbeafe"));
    p.drawPath(path);
    const double bottom = top + drawing.height * scale;
    const double right = left + drawing.width * scale;
    p.setPen(QPen(QColor("#64748b"), 1.0));
    p.drawLine(QPointF(left, bottom + 14), QPointF(left, bottom + 36));
    p.drawLine(QPointF(right, bottom + 14), QPointF(right, bottom + 36));
    p.drawLine(QPointF(left, bottom + 28), QPointF(right, bottom + 28));
    p.drawText(QRectF(left, bottom + 34, right - left, 26), Qt::AlignCenter,
               QString::number(drawing.width, 'g', 8) + QStringLiteral(" мм"));
    p.drawText(QRect(12, height() - 30, width() - 24, 22), Qt::AlignCenter,
               QStringLiteral("Контур CUT · мм · размеры только на экране"));
}
} // namespace dxfgen
