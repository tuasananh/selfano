#include "midi/midi_engine.hpp"

#include <chrono>
#include <ratio>

namespace selfano::midi {

namespace {

const auto kEpochStart = std::chrono::steady_clock::now();

} // namespace

double MidiEngine::now_ms() noexcept {
  const auto elapsed = std::chrono::steady_clock::now() - kEpochStart;
  return std::chrono::duration<double, std::milli>(elapsed).count();
}

} // namespace selfano::midi
