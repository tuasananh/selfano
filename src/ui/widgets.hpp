#pragma once

#include "ui/icons.hpp"

#include "imgui.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace selfano::ui {

enum class ButtonTone : std::uint8_t {
  Primary = 0,     // Solid Apple Blue
  Success = 1,     // Solid Apple Emerald
  Secondary = 2,   // Subtle elevated gray control fill
  SoftAccent = 3,  // Soft translucent blue tint
  Ghost = 4,       // Transparent until hovered
  Destructive = 5, // Soft coral tint
  Warning = 6,     // Soft amber tint
};

enum class StatusTone : std::uint8_t {
  Neutral = 0,
  Accent = 1,
  Success = 2,
  Warning = 3,
  Danger = 4,
};

// Elevated rounded Apple surface card. Always pair with end_card() when returning true (or always,
// just like BeginChild: always call end_card()).
bool begin_card(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f), float padding = 16.0f,
                float rounding = 14.0f, bool auto_resize_y = false, bool elevated = true);
void end_card();

// Tactile Apple button with optional leading vector icon, rounded pill/macOS styling, and
// optional subtle 88-key Piano Remote visual cue (shown only when MIDI keyboard is connected).
[[nodiscard]] bool pill_button(const char* id_and_label, ButtonTone tone = ButtonTone::Secondary,
                               IconId icon = IconId::None, float height = 32.0f,
                               float min_width = 0.0f, bool full_pill = false,
                               const char* key_hint = nullptr);

// Compact square/circular toolbar icon button with optional tooltip and piano key cue.
[[nodiscard]] bool icon_button(const char* id, IconId icon, const char* tooltip = nullptr,
                               bool active = false, float size = 32.0f,
                               ButtonTone tone = ButtonTone::Ghost, const char* key_hint = nullptr);

// Authentic iOS/macOS animated sliding toggle switch.
bool toggle_switch(const char* id, bool* value);

// Full-width row with left label, optional piano key cue, and right-aligned toggle switch.
bool settings_toggle_row(const char* label, const char* id, bool* value,
                         const char* tooltip = nullptr, const char* key_hint = nullptr);

// macOS-style sliding capsule segmented control. Returns true when *current_index changes.
bool segmented_control(const char* id, int* current_index, std::span<const std::string_view> labels,
                       float width = 0.0f, float height = 30.0f);

// Tinted capsule status pill with optional leading dot or vector icon.
void status_pill(std::string_view text, StatusTone tone = StatusTone::Neutral,
                 IconId icon = IconId::None, bool show_dot = false);

// Subtle grayed-out keycap badge for an 88-key piano shortcut (auto-hidden when MIDI disconnected).
void piano_key_hint_badge(std::string_view key_text, bool force_show = false);

// Sleek thin rounded progress capsule bar.
void progress_bar_capsule(float fraction, ImVec2 size, ImVec4 fill_color);

// Smooth anti-aliased Apple Activity-style circular progress ring.
void progress_ring(ImDrawList* dl, ImVec2 center, float radius, float thickness, float fraction,
                   ImU32 track_col, ImU32 fill_col);

// macOS sidebar navigation row with vector icon, label, optional trailing pill badge/dot,
// and optional grayed-out piano key cue.
[[nodiscard]] bool sidebar_nav_item(const char* id, const char* label, IconId icon, bool selected,
                                    const char* badge_text = nullptr, bool pulse_dot = false,
                                    const char* key_hint = nullptr);

// Subtle 1px horizontal hairline divider with balanced vertical spacing.
void hairline_separator(float vertical_pad = 8.0f);

// Uppercase tracked Caption label for sidebar/popover section headers.
void section_kicker(const char* label);

} // namespace selfano::ui
