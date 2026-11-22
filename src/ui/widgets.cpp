#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace selfano::ui {

namespace {

[[nodiscard]] std::string_view visible_label(const char* id_and_label) noexcept {
  if (id_and_label == nullptr) {
    return {};
  }
  const char* hash = std::strstr(id_and_label, "##");
  if (hash != nullptr) {
    return std::string_view(id_and_label, static_cast<std::size_t>(hash - id_and_label));
  }
  return std::string_view(id_and_label);
}

[[nodiscard]] ImVec4 lerp_color(ImVec4 a, ImVec4 b, float t) noexcept {
  t = std::clamp(t, 0.0f, 1.0f);
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}

struct CardFrameState {
  ImVec2 start_pos{};
  float rounding{14.0f};
  bool elevated{true};
};

// Small static stack (max nesting 8) for card shadows/borders
CardFrameState g_card_stack[8]{};
int g_card_depth = 0;

} // namespace

bool begin_card(const char* id, ImVec2 size, float padding, float rounding, bool auto_resize_y,
                bool elevated) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float pad = padding * scale;
  const float rad = rounding * scale;

  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rad);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, elevated ? c.card_bg : c.control_bg);
  ImGui::PushStyleColor(ImGuiCol_Border, c.hairline);

  ImGuiChildFlags child_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
  if (auto_resize_y) {
    child_flags |= ImGuiChildFlags_AutoResizeY;
  }

  // Draw shadow on parent draw list using previous-frame or current cursor rect after EndChild
  // works cleanly via Channels, OR draw subtle shadow right before BeginChild if size is known.
  if (g_card_depth < 8) {
    g_card_stack[g_card_depth++] = CardFrameState{
        .start_pos = ImGui::GetCursorScreenPos(), .rounding = rad, .elevated = elevated};
  }

  return ImGui::BeginChild(id, size, child_flags, ImGuiWindowFlags_NoScrollbar);
}

void end_card() {
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);

  if (g_card_depth > 0) {
    const auto st = g_card_stack[--g_card_depth];
    if (st.elevated) {
      const ImVec2 bmin = ImGui::GetItemRectMin();
      const ImVec2 bmax = ImGui::GetItemRectMax();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      // Subtle bottom elevation hairline + soft underside ambient drop
      dl->AddRect(ImVec2(bmin.x, bmin.y + 1.0f), ImVec2(bmax.x, bmax.y + 2.0f), colors().shadow_mid,
                  st.rounding + 0.5f, 0, 1.5f);
      dl->AddRect(ImVec2(bmin.x - 1.0f, bmin.y + 2.0f), ImVec2(bmax.x + 1.0f, bmax.y + 4.0f),
                  colors().shadow_outer, st.rounding + 1.5f, 0, 2.0f);
    }
  }
}

bool pill_button(const char* id_and_label, ButtonTone tone, IconId icon, float height,
                 float min_width, bool full_pill) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float h = height * scale;
  const std::string_view text = visible_label(id_and_label);

  ScopedFont font_guard(tone == ButtonTone::Primary || tone == ButtonTone::Success
                            ? TypeStyle::Headline
                            : TypeStyle::Body);
  const ImVec2 txt_sz = text.empty() ? ImVec2(0.0f, 0.0f)
                                     : ImGui::CalcTextSize(text.data(), text.data() + text.size());
  const float icon_sz = (icon != IconId::None) ? (14.5f * scale) : 0.0f;
  const float icon_gap = (icon != IconId::None && !text.empty()) ? (7.0f * scale) : 0.0f;
  const float pad_x = (full_pill ? 16.0f : 13.0f) * scale;
  const float content_w = icon_sz + icon_gap + txt_sz.x;
  const float w = std::max(min_width * scale, content_w + pad_x * 2.0f);

  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id_and_label, ImVec2(w, h));
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  const ImGuiID wid = ImGui::GetItemID();
  const float hover_t = animate_float(wid, held ? 1.0f : (hovered ? 0.65f : 0.0f), 18.0f);

  ImVec4 bg_idle{};
  ImVec4 bg_hover{};
  ImVec4 fg{};
  ImU32 border = 0;

  switch (tone) {
  case ButtonTone::Primary:
    bg_idle = c.accent;
    bg_hover = held ? c.accent_active : c.accent_hover;
    fg = c.text_on_accent;
    break;
  case ButtonTone::Success:
    bg_idle = c.success;
    bg_hover = ImVec4(c.success.x * 0.92f, c.success.y * 0.92f, c.success.z * 0.92f, 1.0f);
    fg = c.text_on_accent;
    break;
  case ButtonTone::Secondary:
    bg_idle = c.control_bg;
    bg_hover = held ? c.control_active_bg : c.control_hover_bg;
    fg = c.text_primary;
    border = to_u32(c.hairline);
    break;
  case ButtonTone::SoftAccent:
    bg_idle = c.accent_soft;
    bg_hover = ImVec4(c.accent.x, c.accent.y, c.accent.z, is_dark_theme() ? 0.30f : 0.20f);
    fg = c.accent;
    break;
  case ButtonTone::Ghost:
    bg_idle = ImVec4(c.control_bg.x, c.control_bg.y, c.control_bg.z, 0.0f);
    bg_hover = held ? c.control_active_bg : c.control_hover_bg;
    fg = c.text_primary;
    break;
  case ButtonTone::Destructive:
    bg_idle = c.danger_soft;
    bg_hover = ImVec4(c.danger.x, c.danger.y, c.danger.z, is_dark_theme() ? 0.30f : 0.22f);
    fg = c.danger_text;
    break;
  case ButtonTone::Warning:
    bg_idle = c.warning_soft;
    bg_hover = ImVec4(c.warning.x, c.warning.y, c.warning.z, is_dark_theme() ? 0.30f : 0.22f);
    fg = c.warning_text;
    break;
  }

  const float rounding = full_pill ? (h * 0.5f) : (8.5f * scale);
  const ImVec2 pmax(pos.x + w, pos.y + h);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec4 fill = lerp_color(bg_idle, bg_hover, hover_t);
  if (fill.w > 0.005f) {
    dl->AddRectFilled(pos, pmax, to_u32(fill), rounding);
  }
  if (border != 0) {
    dl->AddRect(pos, pmax, border, rounding, 0, 1.0f);
  }

  const float start_x = pos.x + (w - content_w) * 0.5f;
  const float mid_y = pos.y + h * 0.5f;
  const ImU32 fg_u32 = to_u32(fg);

  if (icon != IconId::None) {
    draw_icon(dl, icon, ImVec2(start_x + icon_sz * 0.5f, mid_y), icon_sz, fg_u32, 1.7f);
  }
  if (!text.empty()) {
    const float tx = start_x + icon_sz + icon_gap;
    const float ty = mid_y - txt_sz.y * 0.5f;
    dl->AddText(ImVec2(std::round(tx), std::round(ty)), fg_u32, text.data(),
                text.data() + text.size());
  }

  return clicked;
}

bool icon_button(const char* id, IconId icon, const char* tooltip, bool active, float size,
                 ButtonTone tone) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float sz = size * scale;
  const ImVec2 pos = ImGui::GetCursorScreenPos();

  const bool clicked = ImGui::InvisibleButton(id, ImVec2(sz, sz));
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  const float hover_t =
      animate_float(ImGui::GetItemID(), held ? 1.0f : (hovered ? 0.7f : 0.0f), 18.0f);

  ImVec4 bg_idle = active ? c.accent_soft
                          : (tone == ButtonTone::Secondary
                                 ? c.control_bg
                                 : ImVec4(c.control_bg.x, c.control_bg.y, c.control_bg.z, 0.0f));
  ImVec4 bg_hover =
      active ? ImVec4(c.accent.x, c.accent.y, c.accent.z, is_dark_theme() ? 0.28f : 0.20f)
             : (held ? c.control_active_bg : c.control_hover_bg);
  ImVec4 fg = active ? c.accent : (hovered ? c.text_primary : c.text_secondary);

  const float rad = 8.0f * scale;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec4 fill = lerp_color(bg_idle, bg_hover, hover_t);
  if (fill.w > 0.005f) {
    dl->AddRectFilled(pos, ImVec2(pos.x + sz, pos.y + sz), to_u32(fill), rad);
  }

  draw_icon(dl, icon, ImVec2(pos.x + sz * 0.5f, pos.y + sz * 0.5f), sz * 0.50f, to_u32(fg), 1.65f);

  if (tooltip != nullptr && hovered) {
    ImGui::SetTooltip("%s", tooltip);
  }
  return clicked;
}

bool toggle_switch(const char* id, bool* value) {
  if (value == nullptr) {
    return false;
  }
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float w = 38.0f * scale;
  const float h = 22.0f * scale;
  const float r = h * 0.5f;

  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
  if (clicked) {
    *value = !(*value);
  }

  const float t = animate_float(ImGui::GetItemID(), *value ? 1.0f : 0.0f, 18.0f);
  const ImVec4 track_off =
      is_dark_theme() ? ImVec4(0.24f, 0.24f, 0.27f, 1.0f) : ImVec4(0.80f, 0.80f, 0.84f, 1.0f);
  const ImVec4 track_col = lerp_color(track_off, c.success, t);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), to_u32(track_col), r);

  const float pad = 2.5f * scale;
  const float thumb_r = r - pad;
  const float cx = pos.x + r + t * (w - 2.0f * r);
  const float cy = pos.y + r;

  // Soft thumb shadow
  dl->AddCircleFilled(ImVec2(cx, cy + 1.0f * scale), thumb_r + 0.5f, IM_COL32(0, 0, 0, 32), 20);
  dl->AddCircleFilled(ImVec2(cx, cy), thumb_r, IM_COL32(255, 255, 255, 255), 20);

  return clicked;
}

bool settings_toggle_row(const char* label, const char* id, bool* value, const char* tooltip) {
  const float avail_w = ImGui::GetContentRegionAvail().x;
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float switch_w = 38.0f * scale;

  ImGui::BeginGroup();
  const float y_start = ImGui::GetCursorPosY();
  ImGui::SetCursorPosY(y_start + 2.0f * scale);
  ImGui::TextUnformatted(label);
  if (tooltip != nullptr && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", tooltip);
  }
  ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_w - switch_w));
  ImGui::SetCursorPosY(y_start);
  const bool changed = toggle_switch(id, value);
  if (tooltip != nullptr && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", tooltip);
  }
  ImGui::EndGroup();
  return changed;
}

bool segmented_control(const char* id, int* current_index, std::span<const std::string_view> labels,
                       float width, float height) {
  if (current_index == nullptr || labels.empty()) {
    return false;
  }
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float h = height * scale;
  const int count = static_cast<int>(labels.size());
  *current_index = std::clamp(*current_index, 0, count - 1);

  ScopedFont font_guard(TypeStyle::Subheadline);
  float total_w = width * scale;
  if (total_w <= 0.0f) {
    float max_lbl = 0.0f;
    for (const auto& lbl : labels) {
      const ImVec2 sz = ImGui::CalcTextSize(lbl.data(), lbl.data() + lbl.size());
      max_lbl = std::max(max_lbl, sz.x);
    }
    total_w = static_cast<float>(count) * (max_lbl + 22.0f * scale) + 6.0f * scale;
  }

  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::PushID(id);
  ImGui::InvisibleButton("##seg_track", ImVec2(total_w, h));
  const ImGuiID anim_id = ImGui::GetItemID();
  const bool hovered = ImGui::IsItemHovered();
  const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

  const float pad = 3.0f * scale;
  const float seg_w = (total_w - 2.0f * pad) / static_cast<float>(count);
  bool changed = false;

  if (clicked && hovered) {
    const float rel_x = ImGui::GetIO().MousePos.x - (pos.x + pad);
    const int idx = std::clamp(static_cast<int>(rel_x / seg_w), 0, count - 1);
    if (idx != *current_index) {
      *current_index = idx;
      changed = true;
    }
  }

  const float anim_idx = animate_float(anim_id, static_cast<float>(*current_index), 18.0f);
  const float track_rad = 8.5f * scale;
  const float pill_rad = 6.5f * scale;

  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(pos, ImVec2(pos.x + total_w, pos.y + h), to_u32(c.control_bg), track_rad);

  const ImVec2 pill_min(pos.x + pad + anim_idx * seg_w, pos.y + pad);
  const ImVec2 pill_max(pill_min.x + seg_w, pos.y + h - pad);

  // Subtle shadow + elevated active capsule
  dl->AddRectFilled(ImVec2(pill_min.x, pill_min.y + 1.0f * scale),
                    ImVec2(pill_max.x, pill_max.y + 1.5f * scale), c.shadow_mid, pill_rad);
  dl->AddRectFilled(pill_min, pill_max, to_u32(c.card_elevated_bg), pill_rad);
  dl->AddRect(pill_min, pill_max, to_u32(c.hairline), pill_rad, 0, 1.0f);

  for (int i = 0; i < count; ++i) {
    const auto lbl = labels[static_cast<std::size_t>(i)];
    const bool is_sel = (i == *current_index);
    const ImVec2 sz = ImGui::CalcTextSize(lbl.data(), lbl.data() + lbl.size());
    const float cx = pos.x + pad + (static_cast<float>(i) + 0.5f) * seg_w - sz.x * 0.5f;
    const float cy = pos.y + (h - sz.y) * 0.5f;
    const ImU32 txt_col = to_u32(is_sel ? c.text_primary : c.text_secondary);
    dl->AddText(ImVec2(std::round(cx), std::round(cy)), txt_col, lbl.data(),
                lbl.data() + lbl.size());
  }

  ImGui::PopID();
  return changed;
}

void status_pill(std::string_view text, StatusTone tone, IconId icon, bool show_dot) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  ScopedFont font_guard(TypeStyle::Caption);

  ImVec4 bg = c.control_bg;
  ImVec4 fg = c.text_secondary;
  ImVec4 dot_col = c.text_tertiary;

  switch (tone) {
  case StatusTone::Neutral:
    break;
  case StatusTone::Accent:
    bg = c.accent_soft;
    fg = c.accent;
    dot_col = c.accent;
    break;
  case StatusTone::Success:
    bg = c.success_soft;
    fg = c.success_text;
    dot_col = c.success;
    break;
  case StatusTone::Warning:
    bg = c.warning_soft;
    fg = c.warning_text;
    dot_col = c.warning;
    break;
  case StatusTone::Danger:
    bg = c.danger_soft;
    fg = c.danger_text;
    dot_col = c.danger;
    break;
  }

  const ImVec2 txt_sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
  const float lead_w =
      (icon != IconId::None) ? (13.0f * scale) : (show_dot ? (8.0f * scale) : 0.0f);
  const float lead_gap = (lead_w > 0.0f && !text.empty()) ? (6.0f * scale) : 0.0f;
  const float pad_x = 10.0f * scale;
  const float h = std::max(24.0f * scale, txt_sz.y + 8.0f * scale);
  const float w = pad_x * 2.0f + lead_w + lead_gap + txt_sz.x;

  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(w, h));
  ImDrawList* dl = ImGui::GetWindowDrawList();

  dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), to_u32(bg), h * 0.5f);
  const float mid_y = pos.y + h * 0.5f;
  float x = pos.x + pad_x;
  if (icon != IconId::None) {
    draw_icon(dl, icon, ImVec2(x + lead_w * 0.5f, mid_y), 12.0f * scale, to_u32(fg), 1.6f);
    x += lead_w + lead_gap;
  } else if (show_dot) {
    dl->AddCircleFilled(ImVec2(x + 3.5f * scale, mid_y), 3.5f * scale, to_u32(dot_col), 12);
    x += lead_w + lead_gap;
  }
  if (!text.empty()) {
    dl->AddText(ImVec2(std::round(x), std::round(mid_y - txt_sz.y * 0.5f)), to_u32(fg), text.data(),
                text.data() + text.size());
  }
}

void progress_bar_capsule(float fraction, ImVec2 size, ImVec4 fill_color) {
  const auto& c = colors();
  const float f = std::clamp(fraction, 0.0f, 1.0f);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float w = (size.x <= 0.0f) ? ImGui::GetContentRegionAvail().x : size.x;
  const float h = size.y;
  ImGui::Dummy(ImVec2(w, h));

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float r = h * 0.5f;
  dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), to_u32(c.control_bg), r);
  if (f > 0.005f) {
    const float fw = std::max(h, w * f);
    dl->AddRectFilled(pos, ImVec2(pos.x + std::min(w, fw), pos.y + h), to_u32(fill_color), r);
  }
}

void progress_ring(ImDrawList* dl, ImVec2 center, float radius, float thickness, float fraction,
                   ImU32 track_col, ImU32 fill_col) {
  if (dl == nullptr || radius <= 1.0f) {
    return;
  }
  const float f = std::clamp(fraction, 0.0f, 1.0f);
  dl->AddCircle(center, radius, track_col, 48, thickness);
  if (f > 0.001f) {
    constexpr float kStart = -1.5707963f; // 12 o'clock
    const float end = kStart + f * 6.2831853f;
    dl->PathClear();
    dl->PathArcTo(center, radius, kStart, end, std::max(12, static_cast<int>(f * 48.0f)));
    dl->PathStroke(fill_col, 0, thickness);
    // Rounded end caps
    const float cap_r = thickness * 0.5f;
    dl->AddCircleFilled(
        ImVec2(center.x + std::cos(kStart) * radius, center.y + std::sin(kStart) * radius), cap_r,
        fill_col, 12);
    dl->AddCircleFilled(
        ImVec2(center.x + std::cos(end) * radius, center.y + std::sin(end) * radius), cap_r,
        fill_col, 12);
  }
}

bool sidebar_nav_item(const char* id, const char* label, IconId icon, bool selected,
                      const char* badge_text, bool pulse_dot) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = 34.0f * scale;
  const ImVec2 pos = ImGui::GetCursorScreenPos();

  const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
  const bool hovered = ImGui::IsItemHovered();
  const float hover_t = animate_float(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 18.0f);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float rad = 8.5f * scale;

  if (selected) {
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), to_u32(c.accent_soft), rad);
  } else if (hover_t > 0.01f) {
    const float a = (is_dark_theme() ? 0.12f : 0.055f) * hover_t;
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), with_alpha(c.text_primary, a), rad);
  }

  const float mid_y = pos.y + h * 0.5f;
  const ImU32 icon_col = to_u32(selected ? c.accent : c.text_secondary);
  const ImU32 txt_col = to_u32(selected ? c.accent : c.text_primary);

  draw_icon(dl, icon, ImVec2(pos.x + 17.0f * scale, mid_y), 15.0f * scale, icon_col, 1.65f);

  {
    ScopedFont font_guard(selected ? TypeStyle::Headline : TypeStyle::Body);
    const ImVec2 sz = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(std::round(pos.x + 34.0f * scale), std::round(mid_y - sz.y * 0.5f)), txt_col,
                label);
  }

  if (badge_text != nullptr && badge_text[0] != '\0') {
    ScopedFont cap_guard(TypeStyle::Caption);
    const ImVec2 bsz = ImGui::CalcTextSize(badge_text);
    const float bh = 18.0f * scale;
    const float bw = std::max(bh, bsz.x + 11.0f * scale);
    const ImVec2 bmin(pos.x + w - bw - 8.0f * scale, mid_y - bh * 0.5f);
    const ImVec2 bmax(bmin.x + bw, bmin.y + bh);
    dl->AddRectFilled(bmin, bmax, to_u32(selected ? c.accent : c.control_bg), bh * 0.5f);
    dl->AddText(ImVec2(std::round(bmin.x + (bw - bsz.x) * 0.5f), std::round(mid_y - bsz.y * 0.5f)),
                to_u32(selected ? c.text_on_accent : c.text_secondary), badge_text);
  } else if (pulse_dot) {
    dl->AddCircleFilled(ImVec2(pos.x + w - 14.0f * scale, mid_y), 4.0f * scale, to_u32(c.success),
                        12);
  }

  return clicked;
}

void hairline_separator(float vertical_pad) {
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float pad = vertical_pad * scale;
  const float w = ImGui::GetContentRegionAvail().x;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(w, pad * 2.0f + 1.0f));
  const float y = std::round(pos.y + pad);
  ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + w, y),
                                      to_u32(colors().hairline), 1.0f);
}

void section_kicker(const char* label) {
  ScopedFont font_guard(TypeStyle::Caption);
  ImGui::PushStyleColor(ImGuiCol_Text, colors().text_tertiary);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
}

} // namespace selfano::ui
