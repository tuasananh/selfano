#include "ui/font_loader.hpp"

#include "ui/smufl_glyphs.hpp"

#include "imgui.h"

#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <vector>

namespace selfano::ui {

namespace {

[[nodiscard]] std::string resolve_smufl_font_path() {
  std::vector<std::filesystem::path> candidates;

  if (const char* base_path = SDL_GetBasePath(); base_path != nullptr) {
    candidates.emplace_back(std::filesystem::path(base_path) / "assets" / "fonts" / "Bravura.otf");
  }
  candidates.emplace_back("assets/fonts/Bravura.otf");
  candidates.emplace_back("/assets/fonts/Bravura.otf");
#ifdef SELFANO_SOURCE_ASSETS_DIR
  candidates.emplace_back(std::filesystem::path(SELFANO_SOURCE_ASSETS_DIR) / "fonts" /
                          "Bravura.otf");
#endif

  std::error_code ec;
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate, ec) && !ec) {
      return candidate.string();
    }
  }
  return {};
}

} // namespace

std::expected<AppFonts, std::string> load_app_fonts(ImGuiIO& io, float ui_font_size_px) {
  static constexpr std::array<ImWchar, 3> kSmuflRanges{0xE000, 0xE4FF, 0};

  ImFontConfig ui_cfg{};
  ui_cfg.SizePixels = ui_font_size_px;
  ImFont* ui_font = io.Fonts->AddFontDefaultVector(&ui_cfg);
  if (ui_font == nullptr) {
    return std::unexpected("Failed to load Dear ImGui default vector UI font");
  }

  const std::string smufl_path = resolve_smufl_font_path();
  if (smufl_path.empty()) {
    return std::unexpected("Could not locate Bravura.otf SMuFL font asset");
  }

  ImFontConfig merge_cfg{};
  merge_cfg.MergeMode = true;
  merge_cfg.ExtraSizeScale = kBravuraEmToPixelHeightScale * 0.7f;
  merge_cfg.GlyphRanges = kSmuflRanges.data();
  if (io.Fonts->AddFontFromFileTTF(smufl_path.c_str(), ui_font_size_px, &merge_cfg,
                                   kSmuflRanges.data()) == nullptr) {
    return std::unexpected("Failed to merge Bravura.otf into UI font: " + smufl_path);
  }

  ImFontConfig smufl_cfg{};
  smufl_cfg.OversampleH = 2;
  smufl_cfg.OversampleV = 2;
  smufl_cfg.PixelSnapH = false;
  ImFont* smufl_font =
      io.Fonts->AddFontFromFileTTF(smufl_path.c_str(), 48.0f, &smufl_cfg, kSmuflRanges.data());
  if (smufl_font == nullptr) {
    return std::unexpected("Failed to load Bravura.otf into Dear ImGui font atlas: " + smufl_path);
  }

  return AppFonts{
      .ui_font = ui_font,
      .smufl_font = smufl_font,
      .loaded_smufl_path = smufl_path,
  };
}

} // namespace selfano::ui
