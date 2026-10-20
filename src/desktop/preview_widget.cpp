#include "desktop/preview_widget.hpp"
#include "dxfgen/geometry.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace dxfgen {
namespace {
QString number(double value) { return QString::number(value, 'g', 12).replace('.', ','); }
QString millimetres(double value) { return number(value) + QStringLiteral(" мм"); }

void arrow(QPainter& painter, QPointF tip, QPointF toward_base) {
    const QPointF base = tip + toward_base * 6;
    const QPointF normal(-toward_base.y() * 2.5, toward_base.x() * 2.5);
    painter.save();
    painter.setBrush(painter.pen().color());
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(QPolygonF{tip, base + normal, base - normal});
    painter.restore();
}
void dimension_line(QPainter& painter, QPointF first, QPointF last) {
    const QPointF delta = last - first;
    const double length = std::hypot(delta.x(), delta.y());
    if (length == 0)
        return;
    const QPointF direction = delta / length;
    const bool outside = length < 24;
    painter.drawLine(outside ? first - direction * 14 : first,
                     outside ? last + direction * 14 : last);
    arrow(painter, first, direction * (outside ? -1 : 1));
    arrow(painter, last, direction * (outside ? 1 : -1));
}
void dimension_text(QPainter& painter, QPointF center, const QString& value, double angle = 0) {
    painter.save();
    painter.translate(center);
    painter.rotate(angle);
    const auto size = painter.fontMetrics().size(Qt::TextSingleLine, value);
    const QRectF box(-size.width() / 2.0 - 5, -size.height() / 2.0 - 2, size.width() + 10,
                     size.height() + 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#f8fafc"));
    painter.drawRoundedRect(box, 3, 3);
    painter.setPen(QColor("#334155"));
    painter.drawText(box, Qt::AlignCenter, value);
    painter.restore();
}
QString dimensions(const Part& part) {
    if (const auto* rectangle = std::get_if<Rectangle>(&part.shape))
        return QStringLiteral("Длина: %1; ширина: %2")
            .arg(millimetres(rectangle->width), millimetres(rectangle->height));
    if (const auto* circle = std::get_if<Circle>(&part.shape))
        return QStringLiteral("Диаметр: %1").arg(millimetres(circle->diameter));
    const auto& washer = std::get<Washer>(part.shape);
    return QStringLiteral("Наружный диаметр: %1; внутренний диаметр: %2")
        .arg(millimetres(washer.outer_diameter), millimetres(washer.inner_diameter));
}
} // namespace
PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 300);
    setObjectName(QStringLiteral("partPreview"));
    setAccessibleName(QStringLiteral("Предпросмотр контуров детали в миллиметрах"));
}
void PreviewWidget::setPart(const Part* part, bool included) {
    part_ = part ? std::optional<Part>(*part) : std::nullopt;
    included_ = included;
    const auto description =
        part_ ? dimensions(*part_) +
                    (part_->thickness
                         ? QStringLiteral("; толщина: %1").arg(millimetres(*part_->thickness))
                         : QString{}) +
                    QStringLiteral("; количество: %1 шт.").arg(part_->quantity) +
                    (included_ ? QString{} : QStringLiteral("; Не в экспорте"))
              : QString{};
    setAccessibleDescription(description);
    setToolTip(description + (part_ && expansion_enabled_
                                  ? QStringLiteral("\nДвойной щелчок — увеличить предпросмотр")
                                  : QString{}));
    update();
}
void PreviewWidget::showExpanded() {
    if (!part_ || !validate(*part_).empty() || !expansion_enabled_)
        return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("expandedPreviewDialog"));
    dialog.setWindowTitle(QStringLiteral("%1 — %2").arg(QString::fromUtf8(shape_name(part_->shape)),
                                                        dimensions(*part_)));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.resize(800, 650);
    auto* layout = new QVBoxLayout(&dialog);
    auto* preview = new PreviewWidget;
    preview->expansion_enabled_ = false;
    preview->setPart(&*part_, included_);
    layout->addWidget(preview, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Закрыть"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}
void PreviewWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        showExpanded();
        event->accept();
    } else {
        QWidget::mouseDoubleClickEvent(event);
    }
}
void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(part_ && !included_ ? "#f1f5f9" : "#f8fafc"));
    p.setOpacity(part_ && !included_ ? 0.35 : 1.0);
    p.setPen(QColor("#e2e8f0"));
    for (int x = 20; x < width(); x += 20)
        for (int y = 20; y < height(); y += 20)
            p.drawPoint(x, y);
    p.setPen(QColor("#64748b"));
    const auto errors = part_ ? validate(*part_) : std::vector<ValidationError>{};
    if (!part_ || !errors.empty()) {
        p.setOpacity(1.0); // Keep validation messages readable.
        p.drawText(rect().adjusted(30, 30, -30, -30), Qt::AlignCenter | Qt::TextWordWrap,
                   part_ ? QString::fromStdString(errors.front().message)
                         : QStringLiteral("Добавьте или выберите деталь"));
        return;
    }
    const auto drawing = make_drawing(*part_);
    const bool rectangle = std::holds_alternative<Rectangle>(part_->shape);
    const bool washer = std::holds_alternative<Washer>(part_->shape);
    const auto metrics = p.fontMetrics();
    const int content_width = width() - 24;
    const int line_height = std::max(20, metrics.height() + 4);
    const auto thickness =
        part_->thickness ? QStringLiteral("t %1").arg(millimetres(*part_->thickness)) : QString{};
    const auto quantity = QStringLiteral("%1 шт.").arg(part_->quantity);
    const bool separate_quantity =
        !thickness.isEmpty() &&
        metrics.horizontalAdvance(thickness) + metrics.horizontalAdvance(quantity) + 16 >
            content_width;
    p.drawText(QRect(12, 8, content_width, line_height), Qt::AlignLeft | Qt::AlignVCenter,
               metrics.elidedText(QString::fromUtf8(shape_name(part_->shape)), Qt::ElideRight,
                                  content_width));
    p.drawText(QRect(12, 8 + line_height, content_width, line_height),
               Qt::AlignLeft | Qt::AlignVCenter, thickness);
    // Keep the complete quantity visible. At larger fonts it gets its own line.
    p.drawText(QRect(12, 8 + line_height * (separate_quantity ? 2 : 1), content_width, line_height),
               Qt::AlignRight | Qt::AlignVCenter, quantity);
    const int header_bottom = 16 + line_height * (separate_quantity ? 3 : 2);
    const QRectF area = QRectF(rect()).adjusted(
        rectangle ? 66 : 32, header_bottom + (washer ? 32 : 4), -30, footer_visible_ ? -82 : -52);
    // Normalize first: dividing screen pixels by a tiny Excel dimension can
    // overflow, even though its finite, positive value is otherwise valid.
    const double extent = std::max(drawing.width, drawing.height);
    const double relative_width = drawing.width / extent;
    const double relative_height = drawing.height / extent;
    const double scale = std::min(area.width() / relative_width, area.height() / relative_height);
    const double visible_width = relative_width * scale;
    const double visible_height = relative_height * scale;
    const double left = area.center().x() - visible_width / 2.0;
    const double top = area.center().y() - visible_height / 2.0;
    const auto point = [&](Point value) {
        return QPointF(left + value.x / extent * scale,
                       top + (drawing.height - value.y) / extent * scale);
    };
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (const auto& circle : drawing.circles)
        path.addEllipse(point(circle.center), circle.radius / extent * scale,
                        circle.radius / extent * scale);
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
    const double bottom = top + visible_height;
    const double right = left + visible_width;
    const QPointF center((left + right) / 2, (top + bottom) / 2);
    if (!rectangle) {
        p.setPen(QPen(QColor("#93b4dc"), 1.0, Qt::DashDotLine));
        p.drawLine(QPointF(left - 9, center.y()), QPointF(right + 9, center.y()));
        p.drawLine(QPointF(center.x(), top - 9), QPointF(center.x(), bottom + 9));
    }
    p.setPen(QPen(QColor("#64748b"), 1.0));
    const double dimension_y = bottom + 22;
    const double extension_y = rectangle ? bottom + 5 : center.y();
    p.drawLine(QPointF(left, extension_y), QPointF(left, dimension_y + 5));
    p.drawLine(QPointF(right, extension_y), QPointF(right, dimension_y + 5));
    dimension_line(p, QPointF(left, dimension_y), QPointF(right, dimension_y));
    dimension_text(p, QPointF(center.x(), dimension_y + 18),
                   (washer      ? QStringLiteral("⌀ нар. ")
                    : rectangle ? QString{}
                                : QStringLiteral("⌀ ")) +
                       millimetres(drawing.width));
    if (rectangle) {
        const double dimension_x = left - 20;
        p.drawLine(QPointF(left - 5, top), QPointF(dimension_x - 5, top));
        p.drawLine(QPointF(left - 5, bottom), QPointF(dimension_x - 5, bottom));
        dimension_line(p, QPointF(dimension_x, top), QPointF(dimension_x, bottom));
        dimension_text(p, QPointF(dimension_x - 19, center.y()), millimetres(drawing.height), -90);
    } else if (washer) {
        const auto& inner = drawing.circles[1];
        const double inner_radius = inner.radius / extent * scale;
        const QPointF anchor =
            center + QPointF(inner_radius * 0.70710678, -inner_radius * 0.70710678);
        const double elbow_y = header_bottom + 28;
        const QPointF elbow(anchor.x() + 14, elbow_y);
        p.drawPolyline(QPolygonF{anchor, elbow, QPointF(center.x(), elbow_y),
                                 QPointF(center.x(), header_bottom + 18)});
        const auto delta = elbow - anchor;
        arrow(p, anchor, delta / std::hypot(delta.x(), delta.y()));
        dimension_text(p, QPointF(center.x(), header_bottom + 12),
                       QStringLiteral("⌀ отв. ") + millimetres(inner.radius * 2));
    }
    if (footer_visible_)
        p.drawText(QRect(12, height() - 30, width() - 24, 22), Qt::AlignCenter,
                   QStringLiteral("Размеры не добавляются в DXF"));
}
} // namespace dxfgen
