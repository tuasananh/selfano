#pragma once

#include <expected>
#include <string>

struct ImFont;
struct ImGuiIO;

namespace selfano::ui {

struct AppFonts {
  ImFont* ui_font{nullptr};
  ImFont* smufl_font{nullptr};
  std::string loaded_smufl_path;
};

[[nodiscard]] std::expected<AppFonts, std::string> load_app_fonts(ImGuiIO& io,
                                                                  float ui_font_size_px = 16.0f);

} // namespace selfano::ui
