#include "ui/companion_view.hpp"

#include "core/chord_symbol.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <format>

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

[[nodiscard]] std::string format_pc_mask_notes(std::uint16_t mask) {
  static constexpr std::array<const char*, 12> kNames{
      "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
  };
  std::string out;
  for (int pc = 0; pc < 12; ++pc) {
    if ((mask & static_cast<std::uint16_t>(1U << pc)) != 0U) {
      if (!out.empty()) {
        out += " ";
      }
      out += kNames[static_cast<std::size_t>(pc)];
    }
  }
  return out;
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
  // Top bar: Phrase picker + Add/Delete + 10:00 Chunk Timer
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Phrase:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(250.0f);
  const std::string preview_lbl = phrases_.empty()
                                      ? std::string("(No phrases - click + New)")
                                      : std::format("{} - {}", phrases_[selected_index_].piece_name,
                                                    phrases_[selected_index_].phrase_name);
  if (ImGui::BeginCombo("##comp_phrase_sel", preview_lbl.c_str())) {
    for (std::size_t i = 0; i < phrases_.size(); ++i) {
      const std::string item_lbl =
          std::format("{} - {} ({})", phrases_[i].piece_name, phrases_[i].phrase_name,
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

  ImGui::SameLine();
  if (ImGui::Button("+ New Phrase")) {
    phrases_.push_back(core::RepertoirePhrase{
        .id = std::format("phrase_{}", ++next_phrase_id_),
        .piece_name = "New Piece",
        .phrase_name = "Phrase A",
        .progression_text = "C -> G -> Am -> F",
    });
    selected_index_ = phrases_.size() - 1U;
    load_editor_from_selected(now_ms);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(phrases_.empty());
  if (ImGui::Button("Delete")) {
    phrases_.erase(phrases_.begin() + static_cast<std::ptrdiff_t>(selected_index_));
    if (selected_index_ >= phrases_.size() && !phrases_.empty()) {
      selected_index_ = phrases_.size() - 1U;
    }
    load_editor_from_selected(now_ms);
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  const int mins = static_cast<int>(chunk_remaining_sec_) / 60;
  const int secs = static_cast<int>(chunk_remaining_sec_) % 60;
  ImGui::Text("  |  Chunk Timer: %02d:%02d", mins, secs);
  ImGui::SameLine();
  if (ImGui::Button(timer_running_ ? "Pause Timer" : "Start Timer")) {
    timer_running_ = !timer_running_;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset 10:00")) {
    timer_running_ = false;
    chunk_remaining_sec_ = 600.0;
  }

  // Inline editor row
  if (!phrases_.empty()) {
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Piece", piece_buf_.data(), piece_buf_.size())) {
      apply_editor_changes(now_ms);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::InputText("Section", phrase_buf_.data(), phrase_buf_.size())) {
      apply_editor_changes(now_ms);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::InputText("Chord Map", prog_buf_.data(), prog_buf_.size())) {
      apply_editor_changes(now_ms);
    }
    if (!parse_error_msg_.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.96f, 0.42f, 0.42f, 1.0f), "Parse error: %s",
                         parse_error_msg_.c_str());
    }
  }

  // Matcher settings & actions
  auto cfg = matcher_.config();
  float dwell_f = static_cast<float>(cfg.min_dwell_ms);
  float win_f = static_cast<float>(cfg.rolling_window_ms);
  bool cfg_dirty = false;

  ImGui::SetNextItemWidth(140.0f);
  if (ImGui::SliderFloat("Min Dwell (ms)", &dwell_f, 0.0f, 1500.0f, "%.0f")) {
    cfg.min_dwell_ms = static_cast<double>(dwell_f);
    cfg_dirty = true;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150.0f);
  if (ImGui::SliderFloat("Window (ms)", &win_f, 400.0f, 3000.0f, "%.0f")) {
    cfg.rolling_window_ms = static_cast<double>(win_f);
    cfg_dirty = true;
  }
  if (cfg_dirty) {
    matcher_.set_config(cfg);
  }
  ImGui::SameLine();
  ImGui::Checkbox("Chime", &chime_enabled_);
  ImGui::SameLine();
  if (ImGui::Button("Reset Bar (Space / CC67)")) {
    reset_phrase(now_ms);
  }
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.32f, 0.88f, 0.52f, 1.0f), " | Clean Reps: %u",
                     matcher_.completed_reps());

  const bool recently_completed =
      (last_rep_flash_ms_ >= 0.0) && ((now_ms - last_rep_flash_ms_) < 2200.0);
  ImGui::TextColored(recently_completed ? ImVec4(0.32f, 0.88f, 0.52f, 1.0f)
                                        : ImVec4(0.75f, 0.80f, 0.88f, 1.0f),
                     "%s", status_banner_.c_str());

  ImGui::Spacing();

  // Large low-glare bar cards
  const auto prog = matcher_.progression();
  const float avail_w = std::max(400.0f, ImGui::GetContentRegionAvail().x);
  const float cards_h = std::clamp(ImGui::GetContentRegionAvail().y - 8.0f, 180.0f, 320.0f);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##companion_cards", ImVec2(avail_w, cards_h));
  ImDrawList* dl = ImGui::GetWindowDrawList();

  dl->AddRectFilled(origin, ImVec2(origin.x + avail_w, origin.y + cards_h),
                    IM_COL32(18, 21, 28, 255), 8.0f);
  dl->AddRect(origin, ImVec2(origin.x + avail_w, origin.y + cards_h), IM_COL32(55, 62, 78, 255),
              8.0f, 0, 1.5f);

  if (prog.empty()) {
    if (ui_font != nullptr) {
      dl->AddText(ui_font, 20.0f, ImVec2(origin.x + 28.0f, origin.y + cards_h * 0.42f),
                  IM_COL32(160, 170, 190, 255),
                  "Add or enter a valid chord progression (e.g. Dm -> Bb -> F -> C)");
    }
    return;
  }

  const float pad = 16.0f;
  const float gap = 12.0f;
  const float card_w = (avail_w - 2.0f * pad - gap * static_cast<float>(prog.size() - 1U)) /
                       static_cast<float>(prog.size());
  const float card_h = cards_h - 2.0f * pad;
  const std::size_t active_bar = matcher_.current_bar_index();
  const std::uint16_t matched_pc = matcher_.matched_pc_mask_for_current_bar();
  const double dwell = matcher_.dwell_progress(now_ms);

  for (std::size_t i = 0; i < prog.size(); ++i) {
    const bool is_active = (i == active_bar);
    const bool is_done = (i < active_bar);
    const ImVec2 c0(origin.x + pad + static_cast<float>(i) * (card_w + gap), origin.y + pad);
    const ImVec2 c1(c0.x + card_w, c0.y + card_h);

    ImU32 bg = IM_COL32(28, 33, 44, 255);
    ImU32 border = IM_COL32(72, 82, 102, 255);
    float border_w = 1.5f;
    if (is_active) {
      bg = IM_COL32(26, 46, 72, 255);
      border = IM_COL32(56, 189, 248, 255);
      border_w = 3.0f;
    } else if (is_done) {
      bg = IM_COL32(22, 48, 38, 255);
      border = IM_COL32(52, 211, 153, 210);
      border_w = 2.0f;
    }

    dl->AddRectFilled(c0, c1, bg, 8.0f);
    dl->AddRect(c0, c1, border, 8.0f, 0, border_w);

    if (ui_font != nullptr) {
      const std::string bar_hdr = std::format("Bar {}", i + 1);
      dl->AddText(ui_font, 16.0f, ImVec2(c0.x + 14.0f, c0.y + 12.0f),
                  is_active ? IM_COL32(125, 211, 252, 255) : IM_COL32(156, 163, 175, 255),
                  bar_hdr.c_str());

      // Big centered chord symbol
      const float sym_size = std::clamp(card_w * 0.24f, 28.0f, 52.0f);
      const ImVec2 sym_sz =
          ui_font->CalcTextSizeA(sym_size, FLT_MAX, 0.0f, prog[i].canonical_text.c_str());
      const ImVec2 sym_pos(c0.x + (card_w - sym_sz.x) * 0.5f, c0.y + (card_h - sym_sz.y) * 0.44f);
      dl->AddText(ui_font, sym_size, sym_pos, IM_COL32(241, 245, 249, 255),
                  prog[i].canonical_text.c_str());

      const std::string tones_str =
          std::format("Tones: {}", format_pc_mask_notes(prog[i].pitch_class_mask));
      dl->AddText(ui_font, 14.0f, ImVec2(c0.x + 14.0f, c1.y - 46.0f), IM_COL32(148, 163, 184, 255),
                  tones_str.c_str());

      if (is_active) {
        const std::string hit_str =
            matched_pc == 0 ? std::string("Listening...")
                            : std::format("Heard: {}", format_pc_mask_notes(matched_pc));
        dl->AddText(ui_font, 14.0f, ImVec2(c0.x + 14.0f, c1.y - 26.0f), IM_COL32(52, 211, 153, 255),
                    hit_str.c_str());

        // Dwell progress bar at card bottom
        const float bar_y0 = c1.y - 8.0f;
        const float bar_y1 = c1.y - 3.0f;
        dl->AddRectFilled(ImVec2(c0.x + 10.0f, bar_y0), ImVec2(c1.x - 10.0f, bar_y1),
                          IM_COL32(40, 50, 68, 255), 2.0f);
        dl->AddRectFilled(
            ImVec2(c0.x + 10.0f, bar_y0),
            ImVec2(c0.x + 10.0f + (card_w - 20.0f) * static_cast<float>(dwell), bar_y1),
            IM_COL32(56, 189, 248, 255), 2.0f);
      } else if (is_done) {
        dl->AddText(ui_font, 14.0f, ImVec2(c0.x + 14.0f, c1.y - 26.0f), IM_COL32(52, 211, 153, 255),
                    "Matched");
      }
    }
  }
}

} // namespace selfano::ui
