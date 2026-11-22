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
    {21, "A0", false, PianoRemoteAction::GoHome, "Today / Home", "Return to Today dashboard"},
    {22, "A#0", true, PianoRemoteAction::ToggleCheatSheet, "Key Map",
     "Show or hide this Piano Remote overlay"},
    {23, "B0", false, PianoRemoteAction::ToggleAutopilot, "30-Min Session",
     "Start or pause guided Autopilot"},

    // Right extreme octave (C7 - C8)
    {96, "C7", false, PianoRemoteAction::PrevStage, "Prev Stage", "Switch to previous stage"},
    {97, "C#7", true, PianoRemoteAction::DecreaseTempoOrKey, "BPM / Key -",
     "Etudes: -5 BPM | Warm-Up/Home: Prev key"},
    {98, "D7", false, PianoRemoteAction::NextStageOrSkipBlock, "Next Stage / Skip",
     "Next stage (Autopilot: Skip block)"},
    {99, "D#7", true, PianoRemoteAction::IncreaseTempoOrKey, "BPM / Key +",
     "Etudes: +5 BPM | Warm-Up/Home: Next key"},
    {100, "E7", false, PianoRemoteAction::ReplayOrHear, "Hear / Replay",
     "Preview step/card or replay ear melody"},
    {101, "F7", false, PianoRemoteAction::PlayPauseTransport, "Play / Pause",
     "Start/pause Etude, Companion timer, or Autopilot"},
    {102, "F#7", true, PianoRemoteAction::SrsAgainOrMetronome, "Again / Metronome",
     "SRS: Rate Again | Else: Toggle metronome/chime"},
    {103, "G7", false, PianoRemoteAction::ResetOrRepeatPass, "Reset / Repeat",
     "Repeat Warm-Up pass, stop Etude, or reset bar"},
    {104, "G#7", true, PianoRemoteAction::SrsHardOrFocusMode, "Hard / Focus",
     "SRS: Rate Hard | Else: Toggle Focus sidebar"},
    {105, "A7", false, PianoRemoteAction::NextPromptOrPassage, "Next / New",
     "Skip card, new Etude/melody, or next phrase"},
    {106, "A#7", true, PianoRemoteAction::SrsGoodOrPanic, "Good / Panic",
     "SRS: Rate Good | Else: All Notes Off"},
    {107, "B7", false, PianoRemoteAction::ContextToggle, "Mode Toggle",
     "Etude timing, Warm-Up octaves, or SRS hint"},
    {108, "C8", false, PianoRemoteAction::ConfirmOrGood, "Confirm / Good",
     "SRS Good, Continue transition, or Repeat pass"},
}};

struct PianoRemoteContext {
  bool enabled{true};
  bool in_midi_monitor{false};
  std::optional<AutopilotStage> active_stage{};
  bool soft_pedal_held{false};
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
