#include "ui/virtual_piano.hpp"

#include "ui/design_system.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>

namespace selfano::ui {

namespace {

constexpr bool is_black_key(int midi_note) noexcept {
  const int pc = midi_note % 12;
  return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

struct KeyRect {
  std::uint8_t midi{0};
  ImVec2 min{};
  ImVec2 max{};
};

} // namespace

void VirtualPiano::release_mouse_key(double now_ms, std::vector<midi::MidiNoteEvent>& out_events) {
  if (mouse_held_note_.has_value()) {
    out_events.push_back(midi::MidiNoteEvent{
        .note = *mouse_held_note_,
        .velocity = 0,
        .is_note_on = false,
        .is_soft_pedal = false,
        .from_hardware = false,
        .timestamp_ms = now_ms,
    });
    mouse_held_note_.reset();
  }
}

void VirtualPiano::draw(const char* id, VirtualPianoMode mode,
                        const VirtualPianoHighlights& highlights, double now_ms,
                        std::vector<midi::MidiNoteEvent>& out_events, float height) {
  const float avail_w = std::max(320.0f, ImGui::GetContentRegionAvail().x);
  const bool use_88 = (mode == VirtualPianoMode::Full88) ||
                      (mode == VirtualPianoMode::Auto && avail_w >= kAutoFull88MinWidth);
  last_was_88_keys_ = use_88;

  const int min_midi = use_88 ? 21 : 36;  // A0 or C2
  const int max_midi = use_88 ? 108 : 84; // C8 or C6

  int white_count = 0;
  for (int m = min_midi; m <= max_midi; ++m) {
    if (!is_black_key(m)) {
      ++white_count;
    }
  }

  const float rail_h = 8.0f;
  const float felt_h = 3.0f;
  const float keys_h = std::max(64.0f, height - rail_h - felt_h);

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, ImVec2(avail_w, height), ImGuiButtonFlags_MouseButtonLeft);
  const bool active = ImGui::IsItemActive();

  const float key_top_y = origin.y + rail_h + felt_h;
  const float key_bot_y = origin.y + height;

  const float white_w = avail_w / static_cast<float>(white_count);
  const float black_w = white_w * 0.60f;
  const float black_h = keys_h * 0.61f;

  std::vector<KeyRect> white_keys;
  std::vector<KeyRect> black_keys;
  white_keys.reserve(static_cast<std::size_t>(white_count));
  black_keys.reserve(static_cast<std::size_t>(max_midi - min_midi + 1 - white_count));

  int white_idx = 0;
  for (int m = min_midi; m <= max_midi; ++m) {
    if (!is_black_key(m)) {
      const float x0 = origin.x + static_cast<float>(white_idx) * white_w;
      white_keys.push_back(KeyRect{
          .midi = static_cast<std::uint8_t>(m),
          .min = ImVec2(x0, key_top_y),
          .max = ImVec2(x0 + white_w, key_bot_y),
      });
      ++white_idx;
    } else {
      const float seam_x = origin.x + static_cast<float>(white_idx) * white_w;
      black_keys.push_back(KeyRect{
          .midi = static_cast<std::uint8_t>(m),
          .min = ImVec2(seam_x - black_w * 0.5f, key_top_y),
          .max = ImVec2(seam_x + black_w * 0.5f, key_top_y + black_h),
      });
    }
  }

  auto hit_test = [&](ImVec2 p) -> std::optional<std::uint8_t> {
    for (const auto& k : black_keys) {
      if (p.x >= k.min.x && p.x <= k.max.x && p.y >= k.min.y && p.y <= k.max.y) {
        return k.midi;
      }
    }
    for (const auto& k : white_keys) {
      if (p.x >= k.min.x && p.x <= k.max.x && p.y >= k.min.y && p.y <= k.max.y) {
        return k.midi;
      }
    }
    return std::nullopt;
  };

  if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const auto hit = hit_test(ImGui::GetIO().MousePos);
    if (hit != mouse_held_note_) {
      release_mouse_key(now_ms, out_events);
      if (hit.has_value()) {
        mouse_held_note_ = *hit;
        out_events.push_back(midi::MidiNoteEvent{
            .note = *hit,
            .velocity = 96,
            .is_note_on = true,
            .is_soft_pedal = false,
            .from_hardware = false,
            .timestamp_ms = now_ms,
        });
      }
    }
  } else if (mouse_held_note_.has_value()) {
    release_mouse_key(now_ms, out_events);
  }

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 outer_min(origin.x, origin.y);
  const ImVec2 outer_max(origin.x + avail_w, origin.y + height);

  // Subtle chassis shadow & anodized housing rail
  draw_card_shadow(dl, outer_min, outer_max, 10.0f, 0.8f);
  dl->AddRectFilled(outer_min, outer_max, IM_COL32(28, 29, 34, 255), 10.0f);

  // Brushed top rail gradient
  dl->AddRectFilledMultiColor(ImVec2(origin.x + 1.0f, origin.y + 1.0f),
                              ImVec2(origin.x + avail_w - 1.0f, origin.y + rail_h),
                              IM_COL32(52, 54, 62, 255), IM_COL32(52, 54, 62, 255),
                              IM_COL32(30, 31, 36, 255), IM_COL32(30, 31, 36, 255));
  // Classic grand-piano crimson felt strip right above key hinges
  dl->AddRectFilled(ImVec2(origin.x + 1.0f, origin.y + rail_h),
                    ImVec2(origin.x + avail_w - 1.0f, origin.y + rail_h + felt_h),
                    IM_COL32(132, 28, 38, 255));

  const ImU32 col_white_top = IM_COL32(252, 252, 250, 255);
  const ImU32 col_white_bot = IM_COL32(240, 240, 236, 255);
  const ImU32 col_white_seam = IM_COL32(186, 188, 196, 255);

  const ImU32 col_held_white_top = IM_COL32(174, 214, 255, 255);
  const ImU32 col_held_white_bot = IM_COL32(118, 184, 255, 255);
  const ImU32 col_correct_white_top = IM_COL32(166, 238, 186, 255);
  const ImU32 col_correct_white_bot = IM_COL32(92, 214, 128, 255);
  const ImU32 col_incorrect_white_top = IM_COL32(255, 178, 174, 255);
  const ImU32 col_incorrect_white_bot = IM_COL32(248, 114, 108, 255);
  const ImU32 col_target_white_top = IM_COL32(255, 244, 206, 255);
  const ImU32 col_target_white_bot = IM_COL32(255, 224, 146, 255);

  ImFont* lbl_font =
      (ds().fonts.ui_semibold != nullptr) ? ds().fonts.ui_semibold : ImGui::GetFont();
  const float lbl_px = draw_font_size(10.5f);

  // 1. White keys with soft ivory vertical sheen & hinge shadow
  for (const auto& k : white_keys) {
    const bool is_held = highlights.held[k.midi] || (mouse_held_note_ == k.midi);
    ImU32 top_col = col_white_top;
    ImU32 bot_col = col_white_bot;

    if (highlights.incorrect[k.midi]) {
      top_col = col_incorrect_white_top;
      bot_col = col_incorrect_white_bot;
    } else if (highlights.correct[k.midi]) {
      top_col = col_correct_white_top;
      bot_col = col_correct_white_bot;
    } else if (is_held) {
      top_col = col_held_white_top;
      bot_col = col_held_white_bot;
    } else if (highlights.target[k.midi]) {
      top_col = col_target_white_top;
      bot_col = col_target_white_bot;
    }

    const ImVec2 kmin(k.min.x + 0.5f, k.min.y);
    const ImVec2 kmax(k.max.x - 0.5f, k.max.y - (is_held ? 0.5f : 2.0f));
    dl->AddRectFilledMultiColor(kmin, kmax, top_col, top_col, bot_col, bot_col);
    // Subtle rounded bottom bevel
    dl->AddRectFilled(ImVec2(kmin.x, kmax.y - 4.0f), kmax, bot_col, 3.5f,
                      ImDrawFlags_RoundCornersBottom);
    // Soft underside lip when unpressed
    if (!is_held) {
      dl->AddRectFilled(ImVec2(kmin.x, kmax.y), ImVec2(kmax.x, k.max.y),
                        IM_COL32(198, 200, 206, 255), 3.0f, ImDrawFlags_RoundCornersBottom);
    }
    // Top hinge ambient shadow
    dl->AddRectFilledMultiColor(kmin, ImVec2(kmax.x, kmin.y + 7.0f), IM_COL32(0, 0, 0, 28),
                                IM_COL32(0, 0, 0, 28), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    dl->AddRect(kmin, ImVec2(kmax.x, k.max.y), col_white_seam, 3.5f, ImDrawFlags_RoundCornersBottom,
                0.85f);

    // Target dot indicator
    if (highlights.target[k.midi]) {
      const float cx = 0.5f * (kmin.x + kmax.x);
      dl->AddCircleFilled(ImVec2(cx, kmax.y - 22.0f), std::min(4.2f, white_w * 0.24f),
                          IM_COL32(217, 119, 6, 255), 14);
    }

    // Octave C markers + Apple Blue dot on Middle C4
    if ((k.midi % 12) == 0 && lbl_font != nullptr) {
      const int octave = static_cast<int>(k.midi) / 12 - 1;
      const std::string label = std::format("C{}", octave);
      const ImVec2 sz = lbl_font->CalcTextSizeA(lbl_px, FLT_MAX, 0.0f, label.c_str());
      if (sz.x + 2.0f <= white_w) {
        const float cx = 0.5f * (kmin.x + kmax.x) - sz.x * 0.5f;
        const ImU32 txt_col =
            (k.midi == 60) ? IM_COL32(0, 122, 255, 255) : IM_COL32(128, 130, 138, 255);
        dl->AddText(lbl_font, lbl_px, ImVec2(std::round(cx), std::round(kmax.y - sz.y - 4.0f)),
                    txt_col, label.c_str());
      }
    }
  }

  // 2. Black keys with cast shadow and sculpted 3D ebony face
  for (const auto& k : black_keys) {
    const bool is_held = highlights.held[k.midi] || (mouse_held_note_ == k.midi);

    // Soft cast shadow onto white keys
    dl->AddRectFilled(ImVec2(k.min.x - 1.0f, k.min.y),
                      ImVec2(k.max.x + 2.5f, k.max.y + (is_held ? 2.0f : 4.5f)),
                      IM_COL32(0, 0, 0, is_held ? 42 : 68), 3.5f, ImDrawFlags_RoundCornersBottom);

    ImU32 body_top = IM_COL32(46, 48, 56, 255);
    ImU32 body_bot = IM_COL32(22, 23, 28, 255);
    ImU32 face_top = IM_COL32(64, 66, 76, 255);
    ImU32 face_bot = IM_COL32(34, 36, 42, 255);

    if (highlights.incorrect[k.midi]) {
      body_top = IM_COL32(220, 56, 48, 255);
      body_bot = IM_COL32(175, 34, 28, 255);
      face_top = IM_COL32(248, 92, 84, 255);
      face_bot = IM_COL32(215, 52, 44, 255);
    } else if (highlights.correct[k.midi]) {
      body_top = IM_COL32(42, 184, 84, 255);
      body_bot = IM_COL32(26, 138, 60, 255);
      face_top = IM_COL32(72, 214, 114, 255);
      face_bot = IM_COL32(40, 178, 82, 255);
    } else if (is_held) {
      body_top = IM_COL32(26, 124, 236, 255);
      body_bot = IM_COL32(12, 92, 198, 255);
      face_top = IM_COL32(64, 158, 255, 255);
      face_bot = IM_COL32(24, 122, 236, 255);
    } else if (highlights.target[k.midi]) {
      body_top = IM_COL32(204, 142, 26, 255);
      body_bot = IM_COL32(158, 102, 12, 255);
      face_top = IM_COL32(236, 174, 48, 255);
      face_bot = IM_COL32(194, 132, 22, 255);
    }

    dl->AddRectFilledMultiColor(k.min, k.max, body_top, body_top, body_bot, body_bot);
    // Inner raised key top face
    const float inset_x = std::max(1.5f, black_w * 0.12f);
    const float inset_bot = is_held ? 3.0f : 6.0f;
    dl->AddRectFilledMultiColor(ImVec2(k.min.x + inset_x, k.min.y + 1.0f),
                                ImVec2(k.max.x - inset_x, k.max.y - inset_bot), face_top, face_top,
                                face_bot, face_bot);
    dl->AddRect(k.min, k.max, IM_COL32(12, 13, 16, 255), 3.0f, ImDrawFlags_RoundCornersBottom,
                1.0f);

    if (highlights.target[k.midi]) {
      const float cx = 0.5f * (k.min.x + k.max.x);
      dl->AddCircleFilled(ImVec2(cx, k.max.y - 11.0f), std::min(3.4f, black_w * 0.28f),
                          IM_COL32(255, 240, 180, 255), 12);
    }
  }
}

} // namespace selfano::ui
