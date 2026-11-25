#include "ui/design_system.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace selfano::ui {

namespace {

[[nodiscard]] DesignSystemContext& g_ds() noexcept {
  static DesignSystemContext ctx{};
  return ctx;
}
std::unordered_map<ImGuiID, float> g_anim_values{};
float g_frame_dt_sec{1.0f / 60.0f};

[[nodiscard]] constexpr ImVec4 rgba(int r, int g, int b, float a = 1.0f) noexcept {
  return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
          static_cast<float>(b) / 255.0f, a};
}

[[nodiscard]] ThemeColors make_light_colors() noexcept {
  return ThemeColors{
      .canvas_bg = rgba(245, 245, 247, 1.0f),        // #F5F5F7
      .sidebar_bg = rgba(235, 236, 240, 1.0f),       // #EBECF0
      .card_bg = rgba(255, 255, 255, 1.0f),          // #FFFFFF
      .card_elevated_bg = rgba(255, 255, 255, 1.0f), // #FFFFFF
      .control_bg = rgba(232, 232, 237, 1.0f),       // #E8E8ED
      .control_hover_bg = rgba(222, 222, 229, 1.0f),
      .control_active_bg = rgba(210, 210, 218, 1.0f),
      .hairline = rgba(0, 0, 0, 0.085f),
      .hairline_strong = rgba(0, 0, 0, 0.14f),

      .text_primary = rgba(29, 29, 31, 1.0f), // #1D1D1F
      .text_secondary = rgba(102, 102, 108, 1.0f),
      .text_tertiary = rgba(142, 142, 147, 1.0f),
      .text_on_accent = rgba(255, 255, 255, 1.0f),

      .accent = rgba(0, 122, 255, 1.0f), // #007AFF
      .accent_hover = rgba(0, 108, 232, 1.0f),
      .accent_active = rgba(0, 92, 204, 1.0f),
      .accent_soft = rgba(0, 122, 255, 0.12f),

      .success = rgba(40, 199, 69, 1.0f),
      .success_text = rgba(24, 128, 56, 1.0f),
      .success_soft = rgba(40, 199, 69, 0.14f),

      .warning = rgba(255, 149, 0, 1.0f),
      .warning_text = rgba(180, 95, 0, 1.0f),
      .warning_soft = rgba(255, 149, 0, 0.14f),

      .danger = rgba(255, 59, 48, 1.0f),
      .danger_text = rgba(204, 34, 28, 1.0f),
      .danger_soft = rgba(255, 59, 48, 0.12f),

      .indigo = rgba(88, 86, 214, 1.0f),
      .indigo_soft = rgba(88, 86, 214, 0.12f),

      .shadow_outer = IM_COL32(15, 23, 42, 8),
      .shadow_mid = IM_COL32(15, 23, 42, 12),
      .shadow_inner = IM_COL32(15, 23, 42, 16),
  };
}

[[nodiscard]] ThemeColors make_dark_colors() noexcept {
  return ThemeColors{
      .canvas_bg = rgba(22, 22, 24, 1.0f),        // #161618
      .sidebar_bg = rgba(28, 28, 31, 1.0f),       // #1C1C1F
      .card_bg = rgba(36, 36, 40, 1.0f),          // #242428
      .card_elevated_bg = rgba(44, 44, 49, 1.0f), // #2C2C31
      .control_bg = rgba(48, 48, 54, 1.0f),       // #303036
      .control_hover_bg = rgba(60, 60, 68, 1.0f),
      .control_active_bg = rgba(72, 72, 82, 1.0f),
      .hairline = rgba(255, 255, 255, 0.09f),
      .hairline_strong = rgba(255, 255, 255, 0.15f),

      .text_primary = rgba(245, 245, 247, 1.0f), // #F5F5F7
      .text_secondary = rgba(161, 161, 168, 1.0f),
      .text_tertiary = rgba(114, 114, 122, 1.0f),
      .text_on_accent = rgba(255, 255, 255, 1.0f),

      .accent = rgba(10, 132, 255, 1.0f), // #0A84FF
      .accent_hover = rgba(44, 150, 255, 1.0f),
      .accent_active = rgba(0, 110, 224, 1.0f),
      .accent_soft = rgba(10, 132, 255, 0.19f),

      .success = rgba(48, 209, 88, 1.0f),
      .success_text = rgba(74, 222, 128, 1.0f),
      .success_soft = rgba(48, 209, 88, 0.18f),

      .warning = rgba(255, 159, 10, 1.0f),
      .warning_text = rgba(255, 184, 77, 1.0f),
      .warning_soft = rgba(255, 159, 10, 0.18f),

      .danger = rgba(255, 69, 58, 1.0f),
      .danger_text = rgba(255, 105, 97, 1.0f),
      .danger_soft = rgba(255, 69, 58, 0.18f),

      .indigo = rgba(110, 108, 238, 1.0f),
      .indigo_soft = rgba(110, 108, 238, 0.20f),

      .shadow_outer = IM_COL32(0, 0, 0, 26),
      .shadow_mid = IM_COL32(0, 0, 0, 38),
      .shadow_inner = IM_COL32(0, 0, 0, 52),
  };
}

void apply_imgui_style(const ThemeColors& c, float dpi_scale) {
  ImGuiStyle style{};
  style.Alpha = 1.0f;
  style.DisabledAlpha = 0.45f;

  style.WindowPadding = ImVec2(14.0f, 14.0f);
  style.FramePadding = ImVec2(11.0f, 6.5f);
  style.CellPadding = ImVec2(8.0f, 6.0f);
  style.ItemSpacing = ImVec2(10.0f, 8.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
  style.ScrollbarSize = 10.0f;
  style.GrabMinSize = 12.0f;

  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBorderSize = 0.0f;

  style.WindowRounding = 0.0f;
  style.ChildRounding = 14.0f;
  style.FrameRounding = 8.0f;
  style.PopupRounding = 12.0f;
  style.ScrollbarRounding = 9.0f;
  style.GrabRounding = 8.0f;
  style.TabRounding = 8.0f;

  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
  style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
  style.SeparatorTextBorderSize = 1.0f;

  ImVec4* col = style.Colors;
  col[ImGuiCol_Text] = c.text_primary;
  col[ImGuiCol_TextDisabled] = c.text_secondary;
  col[ImGuiCol_WindowBg] = c.canvas_bg;
  col[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  col[ImGuiCol_PopupBg] = c.card_elevated_bg;
  col[ImGuiCol_Border] = c.hairline;
  col[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

  col[ImGuiCol_FrameBg] = c.control_bg;
  col[ImGuiCol_FrameBgHovered] = c.control_hover_bg;
  col[ImGuiCol_FrameBgActive] = c.control_active_bg;

  col[ImGuiCol_TitleBg] = c.sidebar_bg;
  col[ImGuiCol_TitleBgActive] = c.sidebar_bg;
  col[ImGuiCol_TitleBgCollapsed] = c.sidebar_bg;
  col[ImGuiCol_MenuBarBg] = c.sidebar_bg;

  col[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  col[ImGuiCol_ScrollbarGrab] =
      ImVec4(c.text_tertiary.x, c.text_tertiary.y, c.text_tertiary.z, 0.35f);
  col[ImGuiCol_ScrollbarGrabHovered] =
      ImVec4(c.text_secondary.x, c.text_secondary.y, c.text_secondary.z, 0.55f);
  col[ImGuiCol_ScrollbarGrabActive] =
      ImVec4(c.text_secondary.x, c.text_secondary.y, c.text_secondary.z, 0.78f);

  col[ImGuiCol_CheckMark] = c.accent;
  col[ImGuiCol_SliderGrab] = c.accent;
  col[ImGuiCol_SliderGrabActive] = c.accent_active;

  col[ImGuiCol_Button] = c.control_bg;
  col[ImGuiCol_ButtonHovered] = c.control_hover_bg;
  col[ImGuiCol_ButtonActive] = c.control_active_bg;

  col[ImGuiCol_Header] = c.accent_soft;
  col[ImGuiCol_HeaderHovered] = ImVec4(c.accent.x, c.accent.y, c.accent.z,
                                       g_ds().active_theme == AppTheme::Dark ? 0.28f : 0.16f);
  col[ImGuiCol_HeaderActive] = ImVec4(c.accent.x, c.accent.y, c.accent.z,
                                      g_ds().active_theme == AppTheme::Dark ? 0.38f : 0.24f);

  col[ImGuiCol_Separator] = c.hairline;
  col[ImGuiCol_SeparatorHovered] = c.accent;
  col[ImGuiCol_SeparatorActive] = c.accent_active;

  col[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  col[ImGuiCol_ResizeGripHovered] = c.accent_soft;
  col[ImGuiCol_ResizeGripActive] = c.accent;

  col[ImGuiCol_Tab] = c.control_bg;
  col[ImGuiCol_TabHovered] = c.control_hover_bg;
  col[ImGuiCol_TabSelected] = c.accent;
  col[ImGuiCol_TabSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  col[ImGuiCol_TabDimmed] = c.control_bg;
  col[ImGuiCol_TabDimmedSelected] = c.control_hover_bg;

  col[ImGuiCol_PlotLines] = c.accent;
  col[ImGuiCol_PlotLinesHovered] = c.accent_hover;
  col[ImGuiCol_PlotHistogram] = c.accent;
  col[ImGuiCol_PlotHistogramHovered] = c.accent_hover;

  col[ImGuiCol_TableHeaderBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  col[ImGuiCol_TableBorderStrong] = c.hairline;
  col[ImGuiCol_TableBorderLight] = c.hairline;
  col[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  col[ImGuiCol_TableRowBgAlt] =
      ImVec4(c.text_primary.x, c.text_primary.y, c.text_primary.z, 0.025f);

  col[ImGuiCol_TextSelectedBg] = c.accent_soft;
  col[ImGuiCol_DragDropTarget] = c.accent;
  col[ImGuiCol_NavCursor] = c.accent;
  col[ImGuiCol_NavWindowingHighlight] = c.accent;
  col[ImGuiCol_NavWindowingDimBg] = rgba(0, 0, 0, 0.25f);
  col[ImGuiCol_ModalWindowDimBg] = rgba(0, 0, 0, 0.40f);

  style.ScaleAllSizes(dpi_scale);
  style.FontScaleDpi = dpi_scale;
  ImGui::GetStyle() = style;
}

} // namespace

void init_design_system(const AppFonts& fonts, AppTheme theme, float dpi_scale, float ui_zoom) {
  auto& ctx = g_ds();
  ctx.fonts = fonts;
  ctx.dpi_scale = (dpi_scale > 0.0f) ? dpi_scale : 1.0f;
  ctx.ui_zoom = std::clamp(ui_zoom, kMinUiZoom, kMaxUiZoom);
  set_app_theme(theme);
}

void set_app_theme(AppTheme theme) {
  auto& ctx = g_ds();
  ctx.active_theme = theme;
  ctx.colors = (theme == AppTheme::Dark) ? make_dark_colors() : make_light_colors();
  apply_imgui_style(ctx.colors, ctx.dpi_scale * ctx.ui_zoom);
}

void set_ui_zoom(float zoom) {
  auto& ctx = g_ds();
  const float snapped = std::round(std::clamp(zoom, kMinUiZoom, kMaxUiZoom) * 20.0f) / 20.0f;
  if (std::abs(snapped - ctx.ui_zoom) < 0.001f) {
    return;
  }
  ctx.ui_zoom = snapped;
  apply_imgui_style(ctx.colors, ctx.dpi_scale * ctx.ui_zoom);
}

void adjust_ui_zoom(float delta) {
  set_ui_zoom(g_ds().ui_zoom + delta);
}

void reset_ui_zoom() {
  set_ui_zoom(1.0f);
}

void set_show_piano_key_hints(bool show) noexcept {
  g_ds().show_piano_key_hints = show;
}

const DesignSystemContext& ds() noexcept {
  return g_ds();
}

const ThemeColors& colors() noexcept {
  return g_ds().colors;
}

AppTheme active_theme() noexcept {
  return g_ds().active_theme;
}

bool is_dark_theme() noexcept {
  return g_ds().active_theme == AppTheme::Dark;
}

float ui_zoom() noexcept {
  return g_ds().ui_zoom;
}

float effective_ui_scale() noexcept {
  const auto& ctx = g_ds();
  return ctx.dpi_scale * ctx.ui_zoom;
}

bool show_piano_key_hints() noexcept {
  return g_ds().show_piano_key_hints;
}

ImU32 to_u32(ImVec4 c) noexcept {
  return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 with_alpha(ImVec4 c, float alpha) noexcept {
  c.w = std::clamp(alpha, 0.0f, 1.0f);
  return ImGui::ColorConvertFloat4ToU32(c);
}

float type_base_size(TypeStyle style) noexcept {
  switch (style) {
  case TypeStyle::LargeTitle:
    return 28.0f;
  case TypeStyle::Title:
    return 20.0f;
  case TypeStyle::Title2:
    return 17.5f;
  case TypeStyle::Headline:
    return 15.0f;
  case TypeStyle::Body:
    return 14.5f;
  case TypeStyle::Subheadline:
    return 13.0f;
  case TypeStyle::Caption:
    return 11.5f;
  case TypeStyle::HeroNumber:
    return 30.0f;
  }
  return 14.5f;
}

bool type_is_semibold(TypeStyle style) noexcept {
  switch (style) {
  case TypeStyle::LargeTitle:
  case TypeStyle::Title:
  case TypeStyle::Title2:
  case TypeStyle::Headline:
  case TypeStyle::Caption:
  case TypeStyle::HeroNumber:
    return true;
  case TypeStyle::Body:
  case TypeStyle::Subheadline:
    return false;
  }
  return false;
}

ImFont* font_for_style(TypeStyle style) noexcept {
  const auto& ctx = g_ds();
  if (type_is_semibold(style) && ctx.fonts.ui_semibold != nullptr) {
    return ctx.fonts.ui_semibold;
  }
  return ctx.fonts.ui_regular;
}

float draw_font_size(float unscaled_px) noexcept {
  const float scale = ImGui::GetStyle().FontScaleDpi;
  return unscaled_px * ((scale > 0.0f) ? scale : 1.0f);
}

ScopedFont::ScopedFont(TypeStyle style) {
  ImFont* f = font_for_style(style);
  if (f != nullptr) {
    ImGui::PushFont(f, type_base_size(style));
    pushed_ = true;
  }
}

ScopedFont::ScopedFont(ImFont* font, float unscaled_px) {
  if (font != nullptr) {
    ImGui::PushFont(font, unscaled_px);
    pushed_ = true;
  }
}

ScopedFont::~ScopedFont() {
  if (pushed_) {
    ImGui::PopFont();
  }
}

void draw_card_shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float intensity) {
  if (dl == nullptr || intensity <= 0.0f) {
    return;
  }
  auto scale_col = [intensity](ImU32 col) -> ImU32 {
    const std::uint32_t a = (col >> IM_COL32_A_SHIFT) & 0xFFU;
    const std::uint32_t sa =
        static_cast<std::uint32_t>(std::clamp(static_cast<float>(a) * intensity, 0.0f, 255.0f));
    return (col & ~(0xFFU << IM_COL32_A_SHIFT)) | (sa << IM_COL32_A_SHIFT);
  };
  const auto& c = g_ds().colors;
  dl->AddRectFilled(ImVec2(min.x - 1.0f, min.y + 3.0f), ImVec2(max.x + 1.0f, max.y + 7.0f),
                    scale_col(c.shadow_outer), rounding + 3.0f);
  dl->AddRectFilled(ImVec2(min.x, min.y + 2.0f), ImVec2(max.x, max.y + 4.0f),
                    scale_col(c.shadow_mid), rounding + 1.5f);
  dl->AddRectFilled(ImVec2(min.x, min.y + 1.0f), ImVec2(max.x, max.y + 2.0f),
                    scale_col(c.shadow_inner), rounding + 0.5f);
}

void begin_design_system_frame(float dt_sec) {
  g_frame_dt_sec = std::clamp(dt_sec, 0.001f, 0.1f);
}

float animate_float(ImGuiID id, float target, float speed) {
  auto it = g_anim_values.find(id);
  if (it == g_anim_values.end()) {
    g_anim_values.emplace(id, target);
    return target;
  }
  const float alpha = 1.0f - std::exp(-speed * g_frame_dt_sec);
  it->second += (target - it->second) * alpha;
  if (std::abs(target - it->second) < 0.002f) {
    it->second = target;
  }
  return it->second;
}

} // namespace selfano::ui
