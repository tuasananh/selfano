#include "core/piano_remote.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace selfano::core;

TEST_CASE("PianoRemote maps all 16 outer 88-key bindings and ignores playable range",
          "[piano_remote]") {
  const PianoRemoteContext ctx{
      .enabled = true,
      .in_midi_monitor = false,
      .active_stage = AutopilotStage::Stage1WarmUp,
      .soft_pedal_held = false,
  };

  for (const auto& b : kPianoRemoteBindings) {
    REQUIRE(is_piano_remote_key(b.midi_note));
    REQUIRE(resolve_piano_remote_action(b.midi_note, ctx) == b.action);
    const auto found = find_piano_remote_binding(b.action);
    REQUIRE(found.has_value());
    REQUIRE(found->midi_note == b.midi_note);
  }

  // Playable core range C1 (24) .. B6 (95) must NEVER trigger remote commands.
  for (MidiPitch m = 24; m <= 95; ++m) {
    REQUIRE_FALSE(is_piano_remote_key(m));
    REQUIRE(resolve_piano_remote_action(m, ctx) == PianoRemoteAction::None);
  }

  REQUIRE(resolve_piano_remote_action(103, ctx) == PianoRemoteAction::ResetOrRepeatPass);
  REQUIRE(resolve_piano_remote_action(21, ctx) == PianoRemoteAction::GoHome);
  REQUIRE(resolve_piano_remote_action(22, ctx) == PianoRemoteAction::ToggleCheatSheet);
  REQUIRE(resolve_piano_remote_action(108, ctx) == PianoRemoteAction::ConfirmOrGood);
}

TEST_CASE("PianoRemote pauses inside MIDI Monitor and gates Companion Mode on Soft Pedal",
          "[piano_remote]") {
  SECTION("Disabled globally returns None") {
    const PianoRemoteContext disabled{.enabled = false,
                                      .in_midi_monitor = false,
                                      .active_stage = AutopilotStage::Stage1WarmUp,
                                      .soft_pedal_held = true};
    REQUIRE(resolve_piano_remote_action(103, disabled) == PianoRemoteAction::None);
    REQUIRE(resolve_piano_remote_action(21, disabled) == PianoRemoteAction::None);
  }

  SECTION("Inside MIDI Monitor all 88 keys pass through for testing") {
    const PianoRemoteContext mon{.enabled = true,
                                 .in_midi_monitor = true,
                                 .active_stage = std::nullopt,
                                 .soft_pedal_held = true};
    REQUIRE(resolve_piano_remote_action(21, mon) == PianoRemoteAction::None);
    REQUIRE(resolve_piano_remote_action(103, mon) == PianoRemoteAction::None);
    REQUIRE(resolve_piano_remote_action(108, mon) == PianoRemoteAction::None);
  }

  SECTION("Companion Mode / Soft-Pedal Guard requires Soft Pedal (CC 67) held") {
    PianoRemoteContext comp{.enabled = true,
                            .in_midi_monitor = false,
                            .active_stage = std::nullopt,
                            .soft_pedal_held = false,
                            .require_soft_pedal_guard = true};
    REQUIRE(resolve_piano_remote_action(103, comp) == PianoRemoteAction::None);
    REQUIRE(resolve_piano_remote_action(21, comp) == PianoRemoteAction::None);

    comp.soft_pedal_held = true;
    REQUIRE(resolve_piano_remote_action(103, comp) == PianoRemoteAction::ResetOrRepeatPass);
    REQUIRE(resolve_piano_remote_action(21, comp) == PianoRemoteAction::GoHome);
  }
}
