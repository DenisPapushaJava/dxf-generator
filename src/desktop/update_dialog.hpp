#pragma once

#include "update/http.hpp"
#include <functional>

class QWidget;
namespace dxfgen {
void show_update_dialog(QWidget* parent, const std::function<bool()>& before_restart,
                        update::HttpGet get = update::http_get);
} // namespace dxfgen
