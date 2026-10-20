#pragma once

#include <QIcon>
#include <QImage>

namespace dxfgen {

// Used by both the application and the optional Windows ICO generation tool.
QImage app_icon_image(int size);
QIcon make_app_icon();

} // namespace dxfgen
