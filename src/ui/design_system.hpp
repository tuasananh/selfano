#pragma once

#include "ui/font_loader.hpp"

#include "imgui.h"

#include <cstdint>

namespace selfano::ui {

enum class AppTheme : std::uint8_t {
  Light = 0,
  Dark = 1,
};

enum class TypeStyle : std::uint8_t {
  LargeTitle = 0,  // 28 px SemiBold
  Title = 1,       // 20 px SemiBold
  Title2 = 2,      // 17.5 px SemiBold
  Headline = 3,    // 15 px SemiBold
  Body = 4,        // 14.5 px Regular
  Subheadline = 5, // 13 px Regular
  Caption = 6,     // 11.5 px SemiBold
  HeroNumber = 7,  // 30 px SemiBold
};

struct ThemeColors {
  ImVec4 canvas_bg;
  ImVec4 sidebar_bg;
  ImVec4 card_bg;
  ImVec4 card_elevated_bg;
  ImVec4 control_bg;
  ImVec4 control_hover_bg;
  ImVec4 control_active_bg;
  ImVec4 hairline;
  ImVec4 hairline_strong;

  ImVec4 text_primary;
  ImVec4 text_secondary;
  ImVec4 text_tertiary;
  ImVec4 text_on_accent;

  ImVec4 accent;
  ImVec4 accent_hover;
  ImVec4 accent_active;
  ImVec4 accent_soft;

  ImVec4 success;
  ImVec4 success_text;
  ImVec4 success_soft;

  ImVec4 warning;
  ImVec4 warning_text;
  ImVec4 warning_soft;

  ImVec4 danger;
  ImVec4 danger_text;
  ImVec4 danger_soft;

  ImVec4 indigo;
  ImVec4 indigo_soft;

  ImU32 shadow_outer;
  ImU32 shadow_mid;
  ImU32 shadow_inner;
};

inline constexpr float kMinUiZoom = 0.75f;
inline constexpr float kMaxUiZoom = 1.75f;
inline constexpr float kUiZoomStep = 0.10f;

struct DesignSystemContext {
  AppTheme active_theme{AppTheme::Light};
  ThemeColors colors{};
  AppFonts fonts{};
  float dpi_scale{1.0f};
  float ui_zoom{1.0f};
};

void init_design_system(const AppFonts& fonts, AppTheme theme, float dpi_scale,
                        float ui_zoom = 1.0f);
void set_app_theme(AppTheme theme);
void set_ui_zoom(float zoom);
void adjust_ui_zoom(float delta);
void reset_ui_zoom();

[[nodiscard]] const DesignSystemContext& ds() noexcept;
[[nodiscard]] const ThemeColors& colors() noexcept;
[[nodiscard]] AppTheme active_theme() noexcept;
[[nodiscard]] bool is_dark_theme() noexcept;
[[nodiscard]] float ui_zoom() noexcept;
[[nodiscard]] float effective_ui_scale() noexcept;

[[nodiscard]] ImU32 to_u32(ImVec4 c) noexcept;
[[nodiscard]] ImU32 with_alpha(ImVec4 c, float alpha) noexcept;

[[nodiscard]] float type_base_size(TypeStyle style) noexcept;
[[nodiscard]] bool type_is_semibold(TypeStyle style) noexcept;
[[nodiscard]] ImFont* font_for_style(TypeStyle style) noexcept;
[[nodiscard]] float draw_font_size(float unscaled_px) noexcept;

class ScopedFont {
public:
  explicit ScopedFont(TypeStyle style);
  ScopedFont(ImFont* font, float unscaled_px);
  ~ScopedFont();

  ScopedFont(const ScopedFont&) = delete;
  ScopedFont& operator=(const ScopedFont&) = delete;

private:
  bool pushed_{false};
};

// Draws a soft 3-pass Apple elevation drop shadow behind a rounded card rectangle.
void draw_card_shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding,
                      float intensity = 1.0f);

// Smooth frame-rate-independent exponential lerp keyed by ImGuiID.
[[nodiscard]] float animate_float(ImGuiID id, float target, float speed = 14.0f);
void begin_design_system_frame(float dt_sec);

} // namespace selfano::ui
