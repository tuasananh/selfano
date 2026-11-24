#include "core/piano_remote.hpp"

namespace selfano::core {

PianoRemoteAction resolve_piano_remote_action(MidiPitch midi_note,
                                              const PianoRemoteContext& ctx) noexcept {
  if (!ctx.enabled || ctx.in_midi_monitor || !is_piano_remote_key(midi_note)) {
    return PianoRemoteAction::None;
  }
  // In Companion Mode or extreme-register lessons (<24 or >=96), require holding Soft Pedal
  // (CC 67) so expressive high/low repertoire notes never accidentally trigger app commands.
  if (ctx.require_soft_pedal_guard && !ctx.soft_pedal_held) {
    return PianoRemoteAction::None;
  }

  for (const auto& binding : kPianoRemoteBindings) {
    if (binding.midi_note == midi_note) {
      return binding.action;
    }
  }
  return PianoRemoteAction::None;
}

std::optional<PianoRemoteBinding> find_piano_remote_binding(PianoRemoteAction action) noexcept {
  if (action == PianoRemoteAction::None) {
    return std::nullopt;
  }
  for (const auto& binding : kPianoRemoteBindings) {
    if (binding.action == action) {
      return binding;
    }
  }
  return std::nullopt;
}

} // namespace selfano::core
