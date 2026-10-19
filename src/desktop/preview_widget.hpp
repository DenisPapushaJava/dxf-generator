#pragma once

#include "dxfgen/part.hpp"
#include <QWidget>

namespace dxfgen {
class PreviewWidget final : public QWidget {
  public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setPart(const Part* part);

  protected:
    void paintEvent(QPaintEvent*) override;

  private:
    std::optional<Part> part_;
};
} // namespace dxfgen
