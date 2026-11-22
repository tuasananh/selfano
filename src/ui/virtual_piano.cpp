#include "ui/virtual_piano.hpp"

#include "imgui.h"

#include <algorithm>
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

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, ImVec2(avail_w, height), ImGuiButtonFlags_MouseButtonLeft);
  const bool active = ImGui::IsItemActive();

  const float white_w = avail_w / static_cast<float>(white_count);
  const float black_w = white_w * 0.62f;
  const float black_h = height * 0.62f;

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
          .min = ImVec2(x0, origin.y),
          .max = ImVec2(x0 + white_w, origin.y + height),
      });
      ++white_idx;
    } else {
      // Positioned straddling the boundary between previous and next white key
      const float seam_x = origin.x + static_cast<float>(white_idx) * white_w;
      black_keys.push_back(KeyRect{
          .midi = static_cast<std::uint8_t>(m),
          .min = ImVec2(seam_x - black_w * 0.5f, origin.y),
          .max = ImVec2(seam_x + black_w * 0.5f, origin.y + black_h),
      });
    }
  }

  // Hit-testing for click and drag glissando (black keys take priority over white keys)
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

  // Chassis bezel
  dl->AddRectFilled(ImVec2(origin.x - 2.0f, origin.y - 3.0f),
                    ImVec2(origin.x + avail_w + 2.0f, origin.y + height + 2.0f),
                    IM_COL32(22, 25, 32, 255), 5.0f);

  const ImU32 col_white_base = IM_COL32(246, 244, 236, 255);
  const ImU32 col_white_border = IM_COL32(68, 72, 82, 255);
  const ImU32 col_black_base = IM_COL32(28, 31, 38, 255);
  const ImU32 col_black_border = IM_COL32(12, 14, 18, 255);

  const ImU32 col_held_white = IM_COL32(130, 194, 255, 255);
  const ImU32 col_held_black = IM_COL32(72, 152, 235, 255);
  const ImU32 col_correct_white = IM_COL32(98, 214, 142, 255);
  const ImU32 col_correct_black = IM_COL32(36, 170, 92, 255);
  const ImU32 col_incorrect_white = IM_COL32(242, 114, 114, 255);
  const ImU32 col_incorrect_black = IM_COL32(214, 62, 62, 255);
  const ImU32 col_target_white = IM_COL32(252, 230, 156, 255);
  const ImU32 col_target_black = IM_COL32(198, 154, 48, 255);

  // Draw white keys first
  for (const auto& k : white_keys) {
    ImU32 fill = col_white_base;
    if (highlights.incorrect[k.midi]) {
      fill = col_incorrect_white;
    } else if (highlights.correct[k.midi]) {
      fill = col_correct_white;
    } else if (highlights.held[k.midi] || mouse_held_note_ == k.midi) {
      fill = col_held_white;
    } else if (highlights.target[k.midi]) {
      fill = col_target_white;
    }

    dl->AddRectFilled(k.min, k.max, fill, 3.0f, ImDrawFlags_RoundCornersBottom);
    dl->AddRect(k.min, k.max, col_white_border, 3.0f, ImDrawFlags_RoundCornersBottom, 1.0f);

    // Target dot indicator near bottom
    if (highlights.target[k.midi]) {
      const float cx = 0.5f * (k.min.x + k.max.x);
      dl->AddCircleFilled(ImVec2(cx, k.max.y - 24.0f), std::min(4.0f, white_w * 0.22f),
                          IM_COL32(204, 134, 24, 255));
    }

    // Octave C labels & Middle C marker
    if ((k.midi % 12) == 0) {
      const int octave = static_cast<int>(k.midi) / 12 - 1;
      const std::string label = std::format("C{}", octave);
      const ImVec2 sz = ImGui::CalcTextSize(label.c_str());
      if (sz.x + 2.0f <= white_w) {
        const float cx = 0.5f * (k.min.x + k.max.x) - sz.x * 0.5f;
        const ImU32 txt_col =
            (k.midi == 60) ? IM_COL32(34, 108, 212, 255) : IM_COL32(95, 100, 110, 255);
        dl->AddText(ImVec2(cx, k.max.y - sz.y - 4.0f), txt_col, label.c_str());
      }
    }
  }

  // Draw black keys on top
  for (const auto& k : black_keys) {
    ImU32 fill = col_black_base;
    if (highlights.incorrect[k.midi]) {
      fill = col_incorrect_black;
    } else if (highlights.correct[k.midi]) {
      fill = col_correct_black;
    } else if (highlights.held[k.midi] || mouse_held_note_ == k.midi) {
      fill = col_held_black;
    } else if (highlights.target[k.midi]) {
      fill = col_target_black;
    }

    dl->AddRectFilled(k.min, k.max, fill, 2.5f, ImDrawFlags_RoundCornersBottom);
    dl->AddRect(k.min, k.max, col_black_border, 2.5f, ImDrawFlags_RoundCornersBottom, 1.0f);

    if (highlights.target[k.midi]) {
      const float cx = 0.5f * (k.min.x + k.max.x);
      dl->AddCircleFilled(ImVec2(cx, k.max.y - 10.0f), std::min(3.5f, black_w * 0.28f),
                          IM_COL32(255, 224, 130, 255));
    }
  }
}

} // namespace selfano::ui
