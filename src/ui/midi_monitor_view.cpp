#include "ui/midi_monitor_view.hpp"

#include "midi/audio_synth.hpp"
#include "midi/midi_engine.hpp"
#include "midi/midi_event.hpp"
#include "midi/midi_scheduler.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/virtual_piano.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace selfano::ui {

namespace {

[[nodiscard]] std::string format_midi_pitch_name(std::uint8_t midi) {
  static constexpr std::array<const char*, 12> kNames{
      "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
  };
  const int octave = static_cast<int>(midi) / 12 - 1;
  return std::format("{}{}", kNames[static_cast<std::size_t>(midi % 12U)], octave);
}

[[nodiscard]] std::string cc_name(std::uint8_t cc) {
  switch (cc) {
  case 1:
    return "Mod Wheel";
  case 7:
    return "Volume";
  case 10:
    return "Pan";
  case 11:
    return "Expression";
  case 64:
    return "Sustain";
  case 66:
    return "Sostenuto";
  case 67:
    return "Soft Pedal";
  case 120:
    return "All Sound Off";
  case 123:
    return "All Notes Off";
  default:
    return std::format("CC {}", cc);
  }
}

void draw_pedal_meter(const char* name, const char* cc_tag, std::uint8_t val) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const bool down = (val >= 64U);

  {
    ScopedFont hf(TypeStyle::Subheadline);
    ImGui::TextUnformatted(name);
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  {
    ScopedFont cap(TypeStyle::Caption);
    ImGui::TextColored(c.text_tertiary, "(%s)", cc_tag);
  }

  const float badge_w = 88.0f * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f * scale, avail_end - badge_w));
  {
    ScopedFont cap(TypeStyle::Caption);
    ImGui::TextColored(down ? c.success_text : c.text_tertiary, "%s  %3u", down ? "DOWN" : "UP  ",
                       static_cast<unsigned>(val));
  }

  progress_bar_capsule(static_cast<float>(val) / 127.0f, ImVec2(-1.0f, 6.0f * scale),
                       down ? c.success : c.accent);
}

} // namespace

void MidiMonitorView::push_log(MidiLogEntry entry) {
  if (boot_ts_ms_ < 0.0) {
    boot_ts_ms_ = entry.timestamp_ms;
  }
  log_.push_front(std::move(entry));
  while (log_.size() > kMaxLogEntries) {
    log_.pop_back();
  }
}

void MidiMonitorView::record_input_event(const midi::MidiNoteEvent& ev) {
  ++total_in_messages_;
  last_channel_ = ev.channel;

  MidiLogEntry entry{};
  entry.is_outbound = false;
  entry.timestamp_ms = ev.timestamp_ms;
  entry.channel = ev.channel;
  entry.kind = ev.kind;
  entry.is_note_on = ev.is_note_on;
  entry.status = ev.status;
  entry.data1 = ev.data1;
  entry.data2 = ev.data2;

  if (ev.is_note_message()) {
    if (ev.note < 128U) {
      held_notes_[ev.note] = ev.is_note_on;
      held_velocities_[ev.note] = ev.is_note_on ? ev.velocity : 0U;
    }
    if (ev.is_note_on) {
      last_velocity_ = ev.velocity;
    }
    if (entry.status == 0U) {
      entry.status = static_cast<std::uint8_t>((ev.is_note_on ? 0x90U : 0x80U) | ev.channel);
      entry.data1 = ev.note;
      entry.data2 = ev.velocity;
    }
    entry.type_label = ev.is_note_on ? "Note On" : "Note Off";
    entry.detail_label = std::format("{} (#{})", format_midi_pitch_name(ev.note), ev.note);
  } else if (ev.is_control_change()) {
    const std::uint8_t cc_num = ev.is_soft_pedal ? 67U : ev.data1;
    const std::uint8_t cc_val = ev.velocity;
    if (cc_num == 64U) {
      sustain_cc_ = cc_val;
    } else if (cc_num == 66U) {
      sostenuto_cc_ = cc_val;
    } else if (cc_num == 67U) {
      soft_cc_ = cc_val;
    } else {
      last_other_cc_ = {cc_num, cc_val};
    }
    if (entry.status == 0U) {
      entry.status = static_cast<std::uint8_t>(0xB0U | ev.channel);
      entry.data1 = cc_num;
      entry.data2 = cc_val;
    }
    entry.type_label = "Control Change";
    entry.detail_label = std::format("{} (CC {})", cc_name(cc_num), cc_num);
  } else if (ev.kind == midi::MidiMessageKind::PitchBend) {
    const int raw14 =
        static_cast<int>((static_cast<unsigned>(ev.data2) << 7U) | static_cast<unsigned>(ev.data1));
    pitch_bend_value_ = raw14 - 8192;
    pitch_bend_seen_ = true;
    entry.type_label = "Pitch Bend";
    entry.detail_label = std::format("{:+d}", pitch_bend_value_);
  } else {
    entry.type_label = "Program Change";
    entry.detail_label = std::format("Program #{}", ev.data1);
  }

  push_log(std::move(entry));
}

void MidiMonitorView::record_outbound_note(std::uint8_t note, std::uint8_t velocity,
                                           std::uint8_t channel, bool is_note_on, double now_ms) {
  ++total_out_messages_;
  MidiLogEntry entry{};
  entry.is_outbound = true;
  entry.timestamp_ms = now_ms;
  entry.channel = channel & 0x0FU;
  entry.kind = midi::MidiMessageKind::Note;
  entry.is_note_on = is_note_on;
  entry.status = static_cast<std::uint8_t>((is_note_on ? 0x90U : 0x80U) | entry.channel);
  entry.data1 = note;
  entry.data2 = is_note_on ? velocity : 0U;
  entry.type_label = is_note_on ? "Note On" : "Note Off";
  entry.detail_label = std::format("{} (#{})", format_midi_pitch_name(note), note);
  push_log(std::move(entry));
}

void MidiMonitorView::record_outbound_cc(std::uint8_t controller, std::uint8_t value,
                                         std::uint8_t channel, double now_ms) {
  ++total_out_messages_;
  MidiLogEntry entry{};
  entry.is_outbound = true;
  entry.timestamp_ms = now_ms;
  entry.channel = channel & 0x0FU;
  entry.kind = midi::MidiMessageKind::ControlChange;
  entry.is_note_on = (value >= 64U);
  entry.status = static_cast<std::uint8_t>(0xB0U | entry.channel);
  entry.data1 = controller;
  entry.data2 = value;
  entry.type_label = "Control Change";
  entry.detail_label = std::format("{} (CC {})", cc_name(controller), controller);
  push_log(std::move(entry));
}

void MidiMonitorView::clear_held_notes() noexcept {
  held_notes_.fill(false);
  held_velocities_.fill(0U);
}

void MidiMonitorView::clear_log() {
  log_.clear();
}

void MidiMonitorView::release_virtual_piano(double now_ms,
                                            std::vector<midi::MidiNoteEvent>& out_events) {
  piano_visualizer_.release_mouse_key(now_ms, out_events);
}

MidiMonitorActions MidiMonitorView::draw(midi::MidiEngine& midi_engine, midi::AudioSynth& synth,
                                         double now_ms,
                                         std::vector<midi::MidiNoteEvent>& out_virtual_events,
                                         std::vector<midi::ScheduledMidiNote>& out_playback_notes,
                                         std::string& status_banner) {
  MidiMonitorActions actions{};
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const std::uint8_t out_ch = static_cast<std::uint8_t>(std::clamp(test_channel_ - 1, 0, 15));
  const std::uint8_t out_vel = static_cast<std::uint8_t>(std::clamp(test_velocity_, 1, 127));
  actions.test_channel = out_ch;

  // Slim Stage Header
  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("MIDI Monitor");
  }
  ImGui::SameLine(0.0f, 12.0f * scale);
  status_pill(midi_engine.is_input_open() ? midi_engine.active_input_name() : "No MIDI IN",
              midi_engine.is_input_open() ? StatusTone::Success : StatusTone::Neutral, IconId::None,
              true);
  ImGui::SameLine(0.0f, 8.0f * scale);
  status_pill("Piano Remote paused while inspecting", StatusTone::Neutral);

  const float right_w = 342.0f * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  if (avail_end > right_w + 340.0f * scale) {
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - right_w));
  }

  if (pill_button("Key Map##mm_keymap", ButtonTone::SoftAccent, IconId::Piano, 32.0f)) {
    actions.open_key_map = true;
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (icon_button("##mm_refresh", IconId::Refresh, "Rescan MIDI Ports", false, 32.0f,
                  ButtonTone::Secondary)) {
    midi_engine.refresh_ports();
    status_banner = std::format("Scanned {} IN, {} OUT port(s)", midi_engine.input_ports().size(),
                                midi_engine.output_ports().size());
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (pill_button("Clear##mm_clear", ButtonTone::Secondary, IconId::Trash, 32.0f)) {
    clear_log();
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (pill_button("Panic##mm_panic", ButtonTone::Destructive, IconId::Stop, 32.0f)) {
    actions.panic_triggered = true;
    clear_held_notes();
    status_banner = "All Notes Off (CC 123)";
  }

  ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

  // Row 1: 3-Column Bento — Held Notes | Pedals & Controllers | MIDI OUT Tester
  const float avail_w = ImGui::GetContentRegionAvail().x;
  const float col_gap = 12.0f * scale;
  const float col_w = (avail_w - 2.0f * col_gap) / 3.0f;
  const float bento_h = 228.0f * scale;

  // Card 1: Active Notes & Velocity
  begin_card("##mm_card_notes", ImVec2(col_w, bento_h), 16.0f, 14.0f, false, true);
  {
    section_kicker("LIVE INPUT NOTES");
    ImGui::Spacing();

    std::vector<std::uint8_t> active_notes;
    for (std::size_t m = 0; m < held_notes_.size(); ++m) {
      if (held_notes_[m]) {
        active_notes.push_back(static_cast<std::uint8_t>(m));
      }
    }

    {
      ScopedFont tf(TypeStyle::Title);
      ImGui::Text("%zu", active_notes.size());
      ImGui::SameLine(0.0f, 8.0f * scale);
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * scale);
      ImGui::TextColored(c.text_secondary, "keys held  \xC2\xB7  Ch %u",
                         static_cast<unsigned>(last_channel_) + 1U);
    }

    ImGui::Spacing();
    if (active_notes.empty()) {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_tertiary,
                         "Press keys on your MIDI piano to inspect notes & velocity.");
    } else {
      const float max_x = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
      for (std::size_t i = 0; i < std::min<std::size_t>(active_notes.size(), 9U); ++i) {
        const std::uint8_t m = active_notes[i];
        const std::string pill_txt =
            std::format("{} #{} v{}", format_midi_pitch_name(m), m, held_velocities_[m]);
        if (i > 0 && ImGui::GetItemRectMax().x + 96.0f * scale <= max_x) {
          ImGui::SameLine(0.0f, 6.0f * scale);
        }
        status_pill(pill_txt, StatusTone::Accent);
      }
    }

    const float rem_y = ImGui::GetContentRegionAvail().y - 42.0f * scale;
    if (rem_y > 4.0f) {
      ImGui::Dummy(ImVec2(0.0f, rem_y));
    }
    {
      ScopedFont cap(TypeStyle::Caption);
      ImGui::TextColored(c.text_secondary,
                         "LAST VELOCITY: %u / 127   \xC2\xB7   IN: %llu  OUT: %llu",
                         static_cast<unsigned>(last_velocity_),
                         static_cast<unsigned long long>(total_in_messages_),
                         static_cast<unsigned long long>(total_out_messages_));
    }
    ImGui::Spacing();
    progress_bar_capsule(static_cast<float>(last_velocity_) / 127.0f, ImVec2(-1.0f, 6.0f * scale),
                         c.accent);
  }
  end_card();

  ImGui::SameLine(0.0f, col_gap);

  // Card 2: Pedals & Controllers
  begin_card("##mm_card_pedals", ImVec2(col_w, bento_h), 16.0f, 14.0f, false, true);
  {
    section_kicker("PEDALS & CONTROLLERS");
    ImGui::Spacing();

    draw_pedal_meter("Sustain (Damper)", "CC 64", sustain_cc_);
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    draw_pedal_meter("Sostenuto", "CC 66", sostenuto_cc_);
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    draw_pedal_meter("Soft (Una Corda)", "CC 67", soft_cc_);

    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    ScopedFont cap(TypeStyle::Caption);
    if (last_other_cc_.has_value()) {
      ImGui::TextColored(c.text_secondary, "Last CC: %s = %u",
                         cc_name(last_other_cc_->first).c_str(),
                         static_cast<unsigned>(last_other_cc_->second));
    } else if (pitch_bend_seen_) {
      ImGui::TextColored(c.text_secondary, "Pitch Bend: %+d", pitch_bend_value_);
    } else {
      ImGui::TextColored(c.text_tertiary,
                         "Press any pedal to verify live CC 64 / 66 / 67 response.");
    }
  }
  end_card();

  ImGui::SameLine(0.0f, col_gap);

  // Card 3: MIDI OUT Tester & Port Picker
  begin_card("##mm_card_out", ImVec2(col_w, bento_h), 16.0f, 14.0f, false, true);
  {
    section_kicker("MIDI OUT TESTER");
    ImGui::Spacing();

    const auto outputs = midi_engine.output_ports();
    const std::string out_lbl =
        midi_engine.is_output_open() ? midi_engine.active_output_name() : "Built-in Software Synth";
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##mm_out_sel", out_lbl.c_str())) {
      if (ImGui::Selectable("Built-in Software Synth", !midi_engine.is_output_open())) {
        midi_engine.close_output_port();
        status_banner = "MIDI OUT set to Built-in Software Synth";
      }
      const auto active_out = midi_engine.active_output_index();
      for (std::size_t i = 0; i < outputs.size(); ++i) {
        const bool sel = active_out.has_value() && *active_out == i;
        const std::string lbl = std::format("{}##mm_out_{}", outputs[i].display_name, i);
        if (ImGui::Selectable(lbl.c_str(), sel)) {
          const auto res = midi_engine.open_output_port(i);
          if (res.has_value()) {
            synth.all_notes_off();
            status_banner = "Connected MIDI OUT: " + outputs[i].display_name;
          } else {
            status_banner = res.error();
          }
        }
      }
      ImGui::EndCombo();
    }

    ImGui::Spacing();
    const float inner_w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(inner_w * 0.60f);
    ImGui::SliderInt("##mm_vel", &test_velocity_, 1, 127, "Vel %d");
    ImGui::SameLine(0.0f, 6.0f * scale);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##mm_ch", &test_channel_, 1, 16, "Ch %d");

    ImGui::Spacing();
    const float half_w = (inner_w - 6.0f * scale) * 0.5f / scale;
    if (pill_button("C Maj Chord##mm_t_chord", ButtonTone::Primary, IconId::VolumeOn, 30.0f,
                    half_w)) {
      const std::array<std::uint8_t, 4> notes{60, 64, 67, 72};
      double off = 0.0;
      for (const std::uint8_t n : notes) {
        out_playback_notes.push_back(midi::ScheduledMidiNote{
            .note = n,
            .velocity = out_vel,
            .start_ms = now_ms + off,
            .duration_ms = 650.0,
            .channel = out_ch,
        });
        off += 55.0;
      }
    }
    ImGui::SameLine(0.0f, 6.0f * scale);
    if (pill_button("Middle C4##mm_t_c4", ButtonTone::SoftAccent, IconId::Play, 30.0f, half_w)) {
      out_playback_notes.push_back(midi::ScheduledMidiNote{
          .note = 60,
          .velocity = out_vel,
          .start_ms = now_ms,
          .duration_ms = 520.0,
          .channel = out_ch,
      });
    }

    ImGui::Spacing();
    if (pill_button("Octave Sweep##mm_t_scale", ButtonTone::Secondary, IconId::Scale, 30.0f,
                    half_w)) {
      const std::array<std::uint8_t, 8> scale_notes{60, 62, 64, 65, 67, 69, 71, 72};
      double off = 0.0;
      for (const std::uint8_t n : scale_notes) {
        out_playback_notes.push_back(midi::ScheduledMidiNote{
            .note = n,
            .velocity = out_vel,
            .start_ms = now_ms + off,
            .duration_ms = 240.0,
            .channel = out_ch,
        });
        off += 150.0;
      }
    }
    ImGui::SameLine(0.0f, 6.0f * scale);
    if (pill_button("Pulse Sustain##mm_t_cc64", ButtonTone::Secondary, IconId::Sparkles, 30.0f,
                    half_w)) {
      actions.test_sustain_pulse = true;
    }
  }
  end_card();

  ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

  // Row 2: Live 88-Key Interactive Visualizer (shows hardware held notes AND clicks send MIDI OUT!)
  {
    ScopedFont cap(TypeStyle::Caption);
    ImGui::TextColored(c.text_tertiary,
                       "LIVE 88-KEY KEYBOARD  \xC2\xB7  HARDWARE MIDI IN LIGHTS UP  \xC2\xB7  "
                       "CLICK KEYS TO TEST MIDI OUT");
  }
  ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
  VirtualPianoHighlights hi{};
  hi.held = held_notes_;
  piano_visualizer_.draw("##mm_live_piano_88", VirtualPianoMode::Full88, hi, now_ms,
                         out_virtual_events, 106.0f * scale);

  ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

  // Row 3: Live Timestamped MIDI Event Log
  begin_card("##mm_log_card", ImVec2(0.0f, 0.0f), 14.0f, 14.0f, false, true);
  {
    section_kicker("LIVE MIDI EVENT LOG (NEWEST FIRST)");
    ImGui::Spacing();

    if (log_.empty()) {
      ScopedFont sub(TypeStyle::Body);
      ImGui::TextColored(c.text_tertiary,
                         "Waiting for MIDI activity. Play keys, press pedals, or click a MIDI OUT "
                         "test button above.");
    } else if (ImGui::BeginTable("##mm_log_table", 7,
                                 static_cast<ImGuiTableFlags>(
                                     static_cast<unsigned>(ImGuiTableFlags_SizingStretchProp) |
                                     static_cast<unsigned>(ImGuiTableFlags_RowBg) |
                                     static_cast<unsigned>(ImGuiTableFlags_ScrollY)))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("Dir", ImGuiTableColumnFlags_WidthFixed, 54.0f * scale);
      ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 76.0f * scale);
      ImGui::TableSetupColumn("Ch", ImGuiTableColumnFlags_WidthFixed, 44.0f * scale);
      ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("Note / Control", ImGuiTableColumnFlags_WidthStretch, 1.3f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 56.0f * scale);
      ImGui::TableSetupColumn("Raw Hex", ImGuiTableColumnFlags_WidthFixed, 96.0f * scale);
      {
        ScopedFont cap(TypeStyle::Caption);
        ImGui::PushStyleColor(ImGuiCol_Text, c.text_tertiary);
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
      }

      for (const auto& item : log_) {
        ImGui::TableNextRow(0, 26.0f * scale);
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(item.is_outbound ? c.success_text : c.accent, "%s",
                           item.is_outbound ? "OUT" : "IN");

        ImGui::TableSetColumnIndex(1);
        const double rel_sec = std::max(0.0, (item.timestamp_ms - boot_ts_ms_) / 1000.0);
        ImGui::TextColored(c.text_tertiary, "+%.2fs", rel_sec);

        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(c.text_secondary, "%u", static_cast<unsigned>(item.channel) + 1U);

        ImGui::TableSetColumnIndex(3);
        const ImVec4 ev_col = (item.kind == midi::MidiMessageKind::ControlChange)
                                  ? c.warning_text
                                  : (item.is_note_on ? c.text_primary : c.text_secondary);
        ImGui::TextColored(ev_col, "%s", item.type_label.c_str());

        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(item.detail_label.c_str());

        ImGui::TableSetColumnIndex(5);
        ImGui::TextColored(c.text_secondary, "%u", static_cast<unsigned>(item.data2));

        ImGui::TableSetColumnIndex(6);
        ImGui::TextColored(c.text_tertiary, "%02X %02X %02X", item.status, item.data1, item.data2);
      }
      ImGui::EndTable();
    }
  }
  end_card();

  return actions;
}

} // namespace selfano::ui
