#include "ui/companion_view.hpp"

#include "core/chord_symbol.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <vector>

namespace selfano::ui {

namespace {

void copy_to_buf(std::span<char> dst, std::string_view src) noexcept {
  if (dst.empty()) {
    return;
  }
  const std::size_t n = std::min(dst.size() - 1U, src.size());
  std::copy_n(src.data(), n, dst.data());
  dst[n] = '\0';
}

[[nodiscard]] const char* pc_name(int pc) noexcept {
  static constexpr std::array<const char*, 12> kNames{
      "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
  };
  return kNames[static_cast<std::size_t>(pc % 12)];
}

} // namespace

CompanionView::CompanionView() {
  phrases_ = {
      core::RepertoirePhrase{
          .id = "gate_of_steiner_a",
          .piece_name = "Gate of Steiner",
          .phrase_name = "Phrase A",
          .progression_text = "Dm -> Bb -> F -> C",
      },
      core::RepertoirePhrase{
          .id = "jane_doe_chorus",
          .piece_name = "Jane Doe",
          .phrase_name = "Chorus",
          .progression_text = "Am -> F -> C -> G",
      },
  };
  load_editor_from_selected(0.0);
}

void CompanionView::load_editor_from_selected(double now_ms) {
  parse_error_msg_.clear();
  if (phrases_.empty()) {
    piece_buf_[0] = '\0';
    phrase_buf_[0] = '\0';
    prog_buf_[0] = '\0';
    matcher_.set_progression({}, now_ms);
    return;
  }
  selected_index_ = std::min(selected_index_, phrases_.size() - 1U);
  const auto& p = phrases_[selected_index_];
  copy_to_buf(piece_buf_, p.piece_name);
  copy_to_buf(phrase_buf_, p.phrase_name);
  copy_to_buf(prog_buf_, p.progression_text);

  const auto parsed = core::parse_chord_progression(p.progression_text);
  if (parsed.has_value()) {
    matcher_.set_progression(*parsed, now_ms);
  } else {
    parse_error_msg_ = parsed.error().message;
    matcher_.set_progression({}, now_ms);
  }
}

void CompanionView::apply_editor_changes(double now_ms) {
  if (phrases_.empty()) {
    return;
  }
  auto& p = phrases_[selected_index_];
  p.piece_name = piece_buf_.data();
  p.phrase_name = phrase_buf_.data();
  p.progression_text = prog_buf_.data();

  const auto parsed = core::parse_chord_progression(p.progression_text);
  if (parsed.has_value()) {
    parse_error_msg_.clear();
    matcher_.set_progression(*parsed, now_ms);
  } else {
    parse_error_msg_ = parsed.error().message;
  }
}

void CompanionView::on_note_on(core::MidiPitch midi, double now_ms, bool& out_chime) {
  const auto ev = matcher_.push_note_on(midi, now_ms);
  if (ev.phrase_completed) {
    last_rep_flash_ms_ = now_ms;
    status_banner_ = std::format("Clean Rep #{} completed!", ev.completed_reps);
    if (chime_enabled_) {
      out_chime = true;
    }
  }
}

void CompanionView::update(double now_ms, double dt_ms, bool& out_chime) {
  if (timer_running_ && dt_ms > 0.0) {
    chunk_remaining_sec_ = std::max(0.0, chunk_remaining_sec_ - dt_ms / 1000.0);
    if (chunk_remaining_sec_ <= 0.0) {
      timer_running_ = false;
    }
  }
  const auto ev = matcher_.update(now_ms);
  if (ev.phrase_completed) {
    last_rep_flash_ms_ = now_ms;
    status_banner_ = std::format("Clean Rep #{} completed!", ev.completed_reps);
    if (chime_enabled_) {
      out_chime = true;
    }
  }
}

void CompanionView::reset_phrase(double now_ms) {
  matcher_.reset_phrase(now_ms);
  status_banner_ = "Phrase reset to Bar 1";
}

std::string CompanionView::next_phrase(double now_ms) {
  if (phrases_.empty()) {
    return {};
  }
  selected_index_ = (selected_index_ + 1U) % phrases_.size();
  load_editor_from_selected(now_ms);
  return std::format("{} \xC2\xB7 {}", phrases_[selected_index_].piece_name,
                     phrases_[selected_index_].phrase_name);
}

void CompanionView::set_phrases(std::vector<core::RepertoirePhrase> phrases, double now_ms) {
  phrases_ = std::move(phrases);
  selected_index_ = 0;
  load_editor_from_selected(now_ms);
}

void CompanionView::apply_preferences(double min_dwell_ms, bool chime_enabled) noexcept {
  auto cfg = matcher_.config();
  cfg.min_dwell_ms = min_dwell_ms;
  matcher_.set_config(cfg);
  chime_enabled_ = chime_enabled;
}

VirtualPianoHighlights
CompanionView::virtual_piano_highlights(const std::array<bool, 128>& held_notes) const {
  VirtualPianoHighlights hi{};
  hi.held = held_notes;
  const auto prog = matcher_.progression();
  const std::size_t bar = matcher_.current_bar_index();
  if (bar < prog.size()) {
    const auto& chord = prog[bar];
    const std::uint16_t matched_mask = matcher_.matched_pc_mask_for_current_bar();
    for (int m = 36; m <= 84; ++m) {
      const std::uint8_t pc = static_cast<std::uint8_t>(m % 12);
      if (chord.contains_pitch_class(pc)) {
        if ((matched_mask & static_cast<std::uint16_t>(1U << pc)) != 0U) {
          hi.correct[static_cast<std::size_t>(m)] = true;
        } else {
          hi.target[static_cast<std::size_t>(m)] = true;
        }
      }
    }
  }
  return hi;
}

void CompanionView::draw(ImFont* ui_font, double now_ms, bool& /*out_chime*/) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;

  // Single Slim Stage Header
  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Companion Mode");
  }
  ImGui::SameLine(0.0f, 12.0f * scale);

  ImGui::SetNextItemWidth(235.0f * scale);
  const std::string preview_lbl =
      phrases_.empty() ? std::string("No phrases yet")
                       : std::format("{} \xC2\xB7 {}", phrases_[selected_index_].piece_name,
                                     phrases_[selected_index_].phrase_name);
  if (ImGui::BeginCombo("##comp_phrase_sel", preview_lbl.c_str())) {
    for (std::size_t i = 0; i < phrases_.size(); ++i) {
      const std::string item_lbl =
          std::format("{} \xC2\xB7 {}  ({})", phrases_[i].piece_name, phrases_[i].phrase_name,
                      phrases_[i].progression_text);
      const bool selected = (i == selected_index_);
      if (ImGui::Selectable(item_lbl.c_str(), selected)) {
        selected_index_ = i;
        load_editor_from_selected(now_ms);
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  if (show_piano_key_hints()) {
    ImGui::SameLine(0.0f, 5.0f * scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.5f * scale);
    piano_key_hint_badge("A7");
  }

  // Right-aligned Chunk Timer + Edit + Options
  const int mins = static_cast<int>(chunk_remaining_sec_) / 60;
  const int secs = static_cast<int>(chunk_remaining_sec_) % 60;
  const float right_w = (show_piano_key_hints() ? 376.0f : 340.0f) * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  if (avail_end > right_w + 350.0f * scale) {
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - right_w));
  }

  status_pill(std::format("{:02d}:{:02d}", mins, secs),
              timer_running_ ? StatusTone::Accent : StatusTone::Neutral);
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (icon_button("##comp_timer_play", timer_running_ ? IconId::Pause : IconId::Play,
                  timer_running_ ? "Pause 10:00 Chunk Timer" : "Start 10:00 Chunk Timer",
                  timer_running_, 32.0f, ButtonTone::Secondary, "F7")) {
    timer_running_ = !timer_running_;
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (icon_button("##comp_timer_rst", IconId::Refresh, "Reset 10:00 Timer & Bar", false, 32.0f,
                  ButtonTone::Secondary, "G7")) {
    timer_running_ = false;
    chunk_remaining_sec_ = 600.0;
    reset_phrase(now_ms);
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (pill_button("Edit##comp_edit", ButtonTone::Secondary, IconId::Edit, 32.0f)) {
    ImGui::OpenPopup("##comp_edit_pop");
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (pill_button("Options##comp_opts", ButtonTone::Secondary, IconId::Sliders, 32.0f)) {
    ImGui::OpenPopup("##comp_opts_pop");
  }

  // Edit Phrase Popover
  if (ImGui::BeginPopup("##comp_edit_pop")) {
    const float pop_w = 320.0f * scale;
    section_kicker("PHRASE & CHORD MAP");
    ImGui::Spacing();
    if (pill_button("New Phrase##comp_new", ButtonTone::SoftAccent, IconId::Plus, 30.0f)) {
      phrases_.push_back(core::RepertoirePhrase{
          .id = std::format("phrase_{}", ++next_phrase_id_),
          .piece_name = "New Piece",
          .phrase_name = "Phrase A",
          .progression_text = "C -> G -> Am -> F",
      });
      selected_index_ = phrases_.size() - 1U;
      load_editor_from_selected(now_ms);
    }
    ImGui::SameLine(0.0f, 8.0f * scale);
    ImGui::BeginDisabled(phrases_.empty());
    if (pill_button("Delete##comp_del", ButtonTone::Destructive, IconId::Trash, 30.0f)) {
      phrases_.erase(phrases_.begin() + static_cast<std::ptrdiff_t>(selected_index_));
      if (selected_index_ >= phrases_.size() && !phrases_.empty()) {
        selected_index_ = phrases_.size() - 1U;
      }
      load_editor_from_selected(now_ms);
    }
    ImGui::EndDisabled();

    if (!phrases_.empty()) {
      hairline_separator(8.0f);
      {
        ScopedFont sub(TypeStyle::Subheadline);
        ImGui::TextColored(c.text_secondary, "Piece Name");
      }
      ImGui::SetNextItemWidth(pop_w);
      if (ImGui::InputText("##comp_piece_in", piece_buf_.data(), piece_buf_.size())) {
        apply_editor_changes(now_ms);
      }

      ImGui::Spacing();
      {
        ScopedFont sub(TypeStyle::Subheadline);
        ImGui::TextColored(c.text_secondary, "Section");
      }
      ImGui::SetNextItemWidth(pop_w);
      if (ImGui::InputText("##comp_sec_in", phrase_buf_.data(), phrase_buf_.size())) {
        apply_editor_changes(now_ms);
      }

      ImGui::Spacing();
      {
        ScopedFont sub(TypeStyle::Subheadline);
        ImGui::TextColored(c.text_secondary, "Chord Progression (e.g. Dm -> Bb -> F -> C)");
      }
      ImGui::SetNextItemWidth(pop_w);
      if (ImGui::InputText("##comp_prog_in", prog_buf_.data(), prog_buf_.size())) {
        apply_editor_changes(now_ms);
      }
      if (!parse_error_msg_.empty()) {
        ImGui::Spacing();
        status_pill(parse_error_msg_, StatusTone::Danger, IconId::AlertTriangle);
      }
    }
    ImGui::EndPopup();
  }

  // Matcher Options Popover
  if (ImGui::BeginPopup("##comp_opts_pop")) {
    const float pop_w = 280.0f * scale;
    section_kicker("HARMONY MATCHER");
    ImGui::Spacing();
    auto cfg = matcher_.config();
    float dwell_f = static_cast<float>(cfg.min_dwell_ms);
    float win_f = static_cast<float>(cfg.rolling_window_ms);
    bool cfg_dirty = false;

    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Minimum Bar Dwell (%.0f ms)",
                         static_cast<double>(dwell_f));
    }
    ImGui::SetNextItemWidth(pop_w);
    if (ImGui::SliderFloat("##comp_dwell_pop", &dwell_f, 0.0f, 1500.0f, "%.0f ms")) {
      cfg.min_dwell_ms = static_cast<double>(dwell_f);
      cfg_dirty = true;
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Rolling Listening Window (%.0f ms)",
                         static_cast<double>(win_f));
    }
    ImGui::SetNextItemWidth(pop_w);
    if (ImGui::SliderFloat("##comp_win_pop", &win_f, 400.0f, 3000.0f, "%.0f ms")) {
      cfg.rolling_window_ms = static_cast<double>(win_f);
      cfg_dirty = true;
    }
    if (cfg_dirty) {
      matcher_.set_config(cfg);
    }

    hairline_separator(8.0f);
    settings_toggle_row("Phrase Completion Chime", "##comp_chime_sw", &chime_enabled_, nullptr,
                        "F#7");
    ImGui::EndPopup();
  }

  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  // Status & Clean Reps Pill Row
  const bool recently_completed =
      (last_rep_flash_ms_ >= 0.0) && ((now_ms - last_rep_flash_ms_) < 2200.0);
  status_pill(std::format("Clean Reps: {}", matcher_.completed_reps()),
              recently_completed ? StatusTone::Success : StatusTone::Accent, IconId::Sparkles);
  ImGui::SameLine(0.0f, 10.0f * scale);
  status_pill(status_banner_, recently_completed ? StatusTone::Success : StatusTone::Neutral);
  ImGui::SameLine(0.0f, 10.0f * scale);
  if (pill_button("Reset Bar##comp_rst_bar", ButtonTone::Ghost, IconId::Refresh, 26.0f, 0.0f, false,
                  "G7")) {
    reset_phrase(now_ms);
  }
  if (show_piano_key_hints()) {
    ImGui::SameLine(0.0f, 10.0f * scale);
    status_pill("Hold Soft Pedal + Key", StatusTone::Neutral);
  }

  ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

  // Hero Tactile Chord Progression Cards
  const auto prog = matcher_.progression();
  const float avail_w = std::max(420.0f, ImGui::GetContentRegionAvail().x);
  const float cards_h = std::clamp(ImGui::GetContentRegionAvail().y - 14.0f, 240.0f, 440.0f);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##companion_cards", ImVec2(avail_w, cards_h));
  ImDrawList* dl = ImGui::GetWindowDrawList();

  ImFont* sb_font = (ds().fonts.ui_semibold != nullptr) ? ds().fonts.ui_semibold : ui_font;
  ImFont* reg_font = (ds().fonts.ui_regular != nullptr) ? ds().fonts.ui_regular : ui_font;

  if (prog.empty()) {
    draw_card_shadow(dl, origin, ImVec2(origin.x + avail_w, origin.y + cards_h), 16.0f, 1.0f);
    dl->AddRectFilled(origin, ImVec2(origin.x + avail_w, origin.y + cards_h), to_u32(c.card_bg),
                      16.0f);
    dl->AddRect(origin, ImVec2(origin.x + avail_w, origin.y + cards_h), to_u32(c.hairline), 16.0f,
                0, 1.0f);
    if (reg_font != nullptr) {
      const char* msg = "Click Edit to add a chord progression (e.g. Dm -> Bb -> F -> C)";
      const float fs = draw_font_size(16.0f);
      const ImVec2 sz = reg_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, msg);
      dl->AddText(reg_font, fs,
                  ImVec2(origin.x + (avail_w - sz.x) * 0.5f, origin.y + (cards_h - sz.y) * 0.5f),
                  to_u32(c.text_secondary), msg);
    }
    return;
  }

  const float gap = 14.0f * scale;
  const float card_w =
      (avail_w - gap * static_cast<float>(prog.size() - 1U)) / static_cast<float>(prog.size());
  const std::size_t active_bar = matcher_.current_bar_index();
  const std::uint16_t matched_pc = matcher_.matched_pc_mask_for_current_bar();
  const double dwell = matcher_.dwell_progress(now_ms);
  const float card_rad = 16.0f * scale;

  for (std::size_t i = 0; i < prog.size(); ++i) {
    const bool is_active = (i == active_bar);
    const bool is_done = (i < active_bar);
    const ImVec2 c0(origin.x + static_cast<float>(i) * (card_w + gap), origin.y);
    const ImVec2 c1(c0.x + card_w, c0.y + cards_h);

    draw_card_shadow(dl, c0, c1, card_rad, is_active ? 1.25f : 0.75f);

    ImU32 bg = to_u32(c.card_bg);
    ImU32 border = to_u32(c.hairline);
    float border_w = 1.0f;
    if (is_active) {
      bg = to_u32(c.card_elevated_bg);
      border = to_u32(c.accent);
      border_w = 2.2f;
    } else if (is_done) {
      bg = to_u32(is_dark_theme() ? ImVec4(0.12f, 0.21f, 0.16f, 1.0f)
                                  : ImVec4(0.93f, 0.99f, 0.95f, 1.0f));
      border = with_alpha(c.success, 0.55f);
      border_w = 1.5f;
    }

    dl->AddRectFilled(c0, c1, bg, card_rad);
    if (is_active) {
      // Subtle top accent bar
      dl->AddRectFilled(ImVec2(c0.x + 18.0f * scale, c0.y + 1.0f),
                        ImVec2(c1.x - 18.0f * scale, c0.y + 4.5f * scale), to_u32(c.accent), 2.5f);
    }
    dl->AddRect(c0, c1, border, card_rad, 0, border_w);

    if (sb_font != nullptr && reg_font != nullptr) {
      // Top-left Bar kicker + top-right check when done
      const std::string bar_hdr = std::format("BAR {}", i + 1);
      const float hdr_px = draw_font_size(12.0f);
      dl->AddText(sb_font, hdr_px, ImVec2(c0.x + 18.0f * scale, c0.y + 18.0f * scale),
                  to_u32(is_active ? c.accent : (is_done ? c.success_text : c.text_tertiary)),
                  bar_hdr.c_str());

      if (is_done) {
        draw_icon(dl, IconId::Check, ImVec2(c1.x - 22.0f * scale, c0.y + 24.0f * scale),
                  15.0f * scale, to_u32(c.success), 2.0f);
      }

      // Large Hero Chord Symbol
      const float sym_size = draw_font_size(std::clamp(card_w * 0.22f / scale, 28.0f, 52.0f));
      const ImVec2 sym_sz =
          sb_font->CalcTextSizeA(sym_size, FLT_MAX, 0.0f, prog[i].canonical_text.c_str());
      const ImVec2 sym_pos(std::round(c0.x + (card_w - sym_sz.x) * 0.5f),
                           std::round(c0.y + (cards_h - sym_sz.y) * 0.40f));
      dl->AddText(sb_font, sym_size, sym_pos, to_u32(c.text_primary),
                  prog[i].canonical_text.c_str());

      // Individual Tone Pills (light up emerald when detected!)
      std::vector<int> chord_pcs;
      for (int pc = 0; pc < 12; ++pc) {
        if ((prog[i].pitch_class_mask & static_cast<std::uint16_t>(1U << pc)) != 0U) {
          chord_pcs.push_back(pc);
        }
      }
      const float tone_px = draw_font_size(12.0f);
      const float pill_h = 22.0f * scale;
      const float pill_gap = 6.0f * scale;
      float row_w = 0.0f;
      std::vector<float> pill_ws;
      for (const int pc : chord_pcs) {
        const ImVec2 tsz = sb_font->CalcTextSizeA(tone_px, FLT_MAX, 0.0f, pc_name(pc));
        const float pw = std::max(28.0f * scale, tsz.x + 14.0f * scale);
        pill_ws.push_back(pw);
        row_w += pw;
      }
      if (chord_pcs.size() > 1U) {
        row_w += pill_gap * static_cast<float>(chord_pcs.size() - 1U);
      }

      float px = c0.x + (card_w - row_w) * 0.5f;
      const float py = c1.y - 54.0f * scale;
      for (std::size_t t = 0; t < chord_pcs.size(); ++t) {
        const int pc = chord_pcs[t];
        const float pw = pill_ws[t];
        const bool hit =
            is_done || (is_active && ((matched_pc & static_cast<std::uint16_t>(1U << pc)) != 0U));
        const ImU32 pbg = to_u32(hit ? c.success_soft : c.control_bg);
        const ImU32 pfg = to_u32(hit ? c.success_text : c.text_secondary);
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + pill_h), pbg, pill_h * 0.5f);
        const char* nm = pc_name(pc);
        const ImVec2 tsz = sb_font->CalcTextSizeA(tone_px, FLT_MAX, 0.0f, nm);
        dl->AddText(
            sb_font, tone_px,
            ImVec2(std::round(px + (pw - tsz.x) * 0.5f), std::round(py + (pill_h - tsz.y) * 0.5f)),
            pfg, nm);
        px += pw + pill_gap;
      }

      // Active dwell progress capsule along card bottom
      if (is_active) {
        const float bar_pad = 16.0f * scale;
        const float bar_y0 = c1.y - 18.0f * scale;
        const float bar_y1 = c1.y - 12.0f * scale;
        const float track_w = std::max(10.0f, card_w - 2.0f * bar_pad);
        dl->AddRectFilled(ImVec2(c0.x + bar_pad, bar_y0), ImVec2(c1.x - bar_pad, bar_y1),
                          to_u32(c.control_bg), 3.0f);
        if (dwell > 0.005) {
          dl->AddRectFilled(ImVec2(c0.x + bar_pad, bar_y0),
                            ImVec2(c0.x + bar_pad + track_w * static_cast<float>(dwell), bar_y1),
                            to_u32(c.accent), 3.0f);
        }
      }
    }
  }
}

} // namespace selfano::ui
