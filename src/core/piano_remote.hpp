#pragma once

#include "core/autopilot.hpp"
#include "core/pitch.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace selfano::core {

enum class PianoRemoteAction : std::uint8_t {
  None = 0,
  GoHome,               // A0  (21)
  ToggleCheatSheet,     // A#0 (22)
  ToggleAutopilot,      // B0  (23)
  PrevStage,            // C7  (96)
  DecreaseTempoOrKey,   // C#7 (97)
  NextStageOrSkipBlock, // D7  (98)
  IncreaseTempoOrKey,   // D#7 (99)
  ReplayOrHear,         // E7  (100)
  PlayPauseTransport,   // F7  (101)
  SrsAgainOrMetronome,  // F#7 (102)
  ResetOrRepeatPass,    // G7  (103)
  SrsHardOrFocusMode,   // G#7 (104)
  NextPromptOrPassage,  // A7  (105)
  SrsGoodOrPanic,       // A#7 (106)
  ContextToggle,        // B7  (107)
  ConfirmOrGood,        // C8  (108)
};

struct PianoRemoteBinding {
  MidiPitch midi_note;
  std::string_view key_name;
  bool is_black_key;
  PianoRemoteAction action;
  std::string_view title;
  std::string_view detail;
};

inline constexpr std::array<PianoRemoteBinding, 16> kPianoRemoteBindings{{
    // Left extreme zone (A0 - B0)
    {.midi_note = 21,
     .key_name = "A0",
     .is_black_key = false,
     .action = PianoRemoteAction::GoHome,
     .title = "Today / Home",
     .detail = "Return to Today dashboard"},
    {.midi_note = 22,
     .key_name = "A#0",
     .is_black_key = true,
     .action = PianoRemoteAction::ToggleCheatSheet,
     .title = "Key Map",
     .detail = "Show or hide this Piano Remote overlay"},
    {.midi_note = 23,
     .key_name = "B0",
     .is_black_key = false,
     .action = PianoRemoteAction::ToggleAutopilot,
     .title = "30-Min Session",
     .detail = "Start or pause guided Autopilot"},

    // Right extreme octave (C7 - C8)
    {.midi_note = 96,
     .key_name = "C7",
     .is_black_key = false,
     .action = PianoRemoteAction::PrevStage,
     .title = "Prev Stage",
     .detail = "Switch to previous stage"},
    {.midi_note = 97,
     .key_name = "C#7",
     .is_black_key = true,
     .action = PianoRemoteAction::DecreaseTempoOrKey,
     .title = "BPM / Key -",
     .detail = "Etudes/Lessons: -5 BPM | Warm-Up/Home: Prev key"},
    {.midi_note = 98,
     .key_name = "D7",
     .is_black_key = false,
     .action = PianoRemoteAction::NextStageOrSkipBlock,
     .title = "Next Stage / Skip",
     .detail = "Next stage (Autopilot: Skip block)"},
    {.midi_note = 99,
     .key_name = "D#7",
     .is_black_key = true,
     .action = PianoRemoteAction::IncreaseTempoOrKey,
     .title = "BPM / Key +",
     .detail = "Etudes/Lessons: +5 BPM | Warm-Up/Home: Next key"},
    {.midi_note = 100,
     .key_name = "E7",
     .is_black_key = false,
     .action = PianoRemoteAction::ReplayOrHear,
     .title = "Hear / Demo",
     .detail = "Listen Demo (Lessons), preview card, or replay ear melody"},
    {.midi_note = 101,
     .key_name = "F7",
     .is_black_key = false,
     .action = PianoRemoteAction::PlayPauseTransport,
     .title = "Play / Pause",
     .detail = "Start/pause Steady Pulse, Etude, or Autopilot"},
    {.midi_note = 102,
     .key_name = "F#7",
     .is_black_key = true,
     .action = PianoRemoteAction::SrsAgainOrMetronome,
     .title = "Again / Metronome",
     .detail = "SRS: Rate Again | Else: Toggle metronome/chime"},
    {.midi_note = 103,
     .key_name = "G7",
     .is_black_key = false,
     .action = PianoRemoteAction::ResetOrRepeatPass,
     .title = "Reset / Repeat",
     .detail = "Reset phrase/pass, stop Etude, or reset bar"},
    {.midi_note = 104,
     .key_name = "G#7",
     .is_black_key = true,
     .action = PianoRemoteAction::SrsHardOrFocusMode,
     .title = "Hard / Focus",
     .detail = "SRS: Rate Hard | Else: Toggle Focus sidebar"},
    {.midi_note = 105,
     .key_name = "A7",
     .is_black_key = false,
     .action = PianoRemoteAction::NextPromptOrPassage,
     .title = "Next / Phrase",
     .detail = "Cycle lesson phrase, skip card, or new Etude/melody"},
    {.midi_note = 106,
     .key_name = "A#7",
     .is_black_key = true,
     .action = PianoRemoteAction::SrsGoodOrPanic,
     .title = "Good / Panic",
     .detail = "SRS: Rate Good | Else: All Notes Off"},
    {.midi_note = 107,
     .key_name = "B7",
     .is_black_key = false,
     .action = PianoRemoteAction::ContextToggle,
     .title = "Mode Toggle",
     .detail = "Cycle lesson mode, Etude timing, Warm-Up octaves, or SRS hint"},
    {.midi_note = 108,
     .key_name = "C8",
     .is_black_key = false,
     .action = PianoRemoteAction::ConfirmOrGood,
     .title = "Confirm / Promote",
     .detail = "SRS Good, Continue transition, or Promote to SRS"},
}};

struct PianoRemoteContext {
  bool enabled{true};
  bool in_midi_monitor{false};
  std::optional<AutopilotStage> active_stage{};
  bool soft_pedal_held{false};
  bool require_soft_pedal_guard{false};
};

// Returns true if `midi_note` belongs to one of the 16 dedicated outer Piano Remote keys.
[[nodiscard]] constexpr bool is_piano_remote_key(MidiPitch midi_note) noexcept {
  return (midi_note >= 21U && midi_note <= 23U) || (midi_note >= 96U && midi_note <= 108U);
}

// Evaluates whether a NoteOn on `midi_note` should trigger a Piano Remote action in `ctx`.
[[nodiscard]] PianoRemoteAction resolve_piano_remote_action(MidiPitch midi_note,
                                                            const PianoRemoteContext& ctx) noexcept;

// Looks up metadata for a given PianoRemoteAction (nullopt for None).
[[nodiscard]] std::optional<PianoRemoteBinding>
find_piano_remote_binding(PianoRemoteAction action) noexcept;

} // namespace selfano::core
