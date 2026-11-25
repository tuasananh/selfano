#include "ui/font_loader.hpp"

#include "ui/smufl_glyphs.hpp"

#include "imgui.h"

#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

namespace selfano::ui {

namespace {

[[nodiscard]] std::string resolve_font_asset(std::string_view filename) {
  std::vector<std::filesystem::path> candidates;

  if (const char* base_path = SDL_GetBasePath(); base_path != nullptr) {
    candidates.emplace_back(std::filesystem::path(base_path) / "assets" / "fonts" / filename);
  }
  candidates.emplace_back(std::filesystem::path("assets") / "fonts" / filename);
  candidates.emplace_back(std::filesystem::path("/assets") / "fonts" / filename);
#ifdef SELFANO_SOURCE_ASSETS_DIR
  candidates.emplace_back(std::filesystem::path(SELFANO_SOURCE_ASSETS_DIR) / "fonts" / filename);
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
  static constexpr std::array<ImWchar, 11> kUiRanges{
      0x0020, 0x00FF, // Basic Latin + Latin Supplement (includes middot, degree, etc.)
      0x2010, 0x2027, // Dashes, quotes, bullet, ellipsis
      0x2190, 0x2199, // Arrows
      0x266D, 0x266F, // Musical flat, natural, sharp
      0x2713, 0x2714, // Checkmarks
      0,
  };
  static constexpr std::array<ImWchar, 9> kSmuflRanges{
      0xE000,
      0xE4FF, // Clefs, brace, time sigs, noteheads, flags, accidentals, articulations, rests
      0xE520,
      0xE53F, // Dynamics (p, m, f, pp, mp, mf, ff)
      0xE650,
      0xE65F, // Keyboard pedal (Ped., *)
      0xEAA0,
      0xEABF, // Arpeggiato wiggle
      0,
  };

  const std::string regular_path = resolve_font_asset("Inter-Regular.ttf");
  const std::string semibold_path = resolve_font_asset("Inter-SemiBold.ttf");
  const std::string smufl_path = resolve_font_asset("Bravura.otf");
  if (smufl_path.empty()) {
    return std::unexpected("Could not locate Bravura.otf SMuFL font asset");
  }

  ImFont* ui_regular = nullptr;
  if (!regular_path.empty()) {
    ImFontConfig reg_cfg{};
    reg_cfg.OversampleH = 2;
    reg_cfg.OversampleV = 2;
    reg_cfg.PixelSnapH = true;
    ui_regular = io.Fonts->AddFontFromFileTTF(regular_path.c_str(), ui_font_size_px, &reg_cfg,
                                              kUiRanges.data());
  }
  if (ui_regular == nullptr) {
    ImFontConfig fallback_cfg{};
    fallback_cfg.SizePixels = ui_font_size_px;
    ui_regular = io.Fonts->AddFontDefaultVector(&fallback_cfg);
  }
  if (ui_regular == nullptr) {
    return std::unexpected("Failed to load primary UI font");
  }

  // Merge Bravura glyphs into primary UI font for inline accidentals/symbols
  ImFontConfig merge_cfg{};
  merge_cfg.MergeMode = true;
  merge_cfg.ExtraSizeScale = kBravuraEmToPixelHeightScale * 0.7f;
  merge_cfg.GlyphRanges = kSmuflRanges.data();
  if (io.Fonts->AddFontFromFileTTF(smufl_path.c_str(), ui_font_size_px, &merge_cfg,
                                   kSmuflRanges.data()) == nullptr) {
    return std::unexpected("Failed to merge Bravura.otf into UI font: " + smufl_path);
  }

  ImFont* ui_semibold = ui_regular;
  if (!semibold_path.empty()) {
    ImFontConfig sb_cfg{};
    sb_cfg.OversampleH = 2;
    sb_cfg.OversampleV = 2;
    sb_cfg.PixelSnapH = true;
    ImFont* loaded_sb = io.Fonts->AddFontFromFileTTF(semibold_path.c_str(), ui_font_size_px,
                                                     &sb_cfg, kUiRanges.data());
    if (loaded_sb != nullptr) {
      ui_semibold = loaded_sb;
    }
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

  io.FontDefault = ui_regular;

  return AppFonts{
      .ui_regular = ui_regular,
      .ui_semibold = ui_semibold,
      .ui_font = ui_regular,
      .smufl_font = smufl_font,
      .loaded_smufl_path = smufl_path,
  };
}

} // namespace selfano::ui
