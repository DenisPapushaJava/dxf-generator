#pragma once

#include "dxfgen/part.hpp"
#include <QWidget>

namespace dxfgen {
class PreviewWidget final : public QWidget {
  public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setPart(const Part* part, bool included = true);
    void setFooterVisible(bool visible) {
        footer_visible_ = visible;
        update();
    }
    void showExpanded();

  protected:
    void paintEvent(QPaintEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

  private:
    std::optional<Part> part_;
    bool included_ = true;
    bool expansion_enabled_ = true;
    bool footer_visible_ = true;
};
} // namespace dxfgen
