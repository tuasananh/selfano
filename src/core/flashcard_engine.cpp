#include "core/flashcard_engine.hpp"

#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/theory.hpp"
#include "core/weekly_key.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

inline constexpr std::array<FlashcardShapeMeta, 40> kShapeBank{{
    // 10 Landmark anchors
    {.shape_id = "landmark:treble:C4",
     .display_name = "Landmark: Middle C4 (Treble)",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Treble},
    {.shape_id = "landmark:treble:G4",
     .display_name = "Landmark: Treble G4",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Treble},
    {.shape_id = "landmark:treble:C5",
     .display_name = "Landmark: Treble C5",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Treble},
    {.shape_id = "landmark:treble:G5",
     .display_name = "Landmark: Treble G5",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Treble},
    {.shape_id = "landmark:treble:C6",
     .display_name = "Landmark: High C6",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Treble},
    {.shape_id = "landmark:bass:C4",
     .display_name = "Landmark: Middle C4 (Bass)",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Bass},
    {.shape_id = "landmark:bass:F3",
     .display_name = "Landmark: Bass F3",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Bass},
    {.shape_id = "landmark:bass:C3",
     .display_name = "Landmark: Bass C3",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Bass},
    {.shape_id = "landmark:bass:F2",
     .display_name = "Landmark: Bass F2",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Bass},
    {.shape_id = "landmark:bass:C2",
     .display_name = "Landmark: Low C2",
     .kind = FlashcardShapeKind::Landmark,
     .clef = Clef::Bass},

    // 12 Treble intervals (6 harmonic + 6 melodic)
    {.shape_id = "interval:treble:harmonic:2nd",
     .display_name = "Treble Harmonic 2nd",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:harmonic:3rd",
     .display_name = "Treble Harmonic 3rd",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:harmonic:4th",
     .display_name = "Treble Harmonic 4th",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:harmonic:5th",
     .display_name = "Treble Harmonic 5th",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:harmonic:6th",
     .display_name = "Treble Harmonic 6th",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:harmonic:octave",
     .display_name = "Treble Harmonic Octave",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:melodic:2nd",
     .display_name = "Treble Melodic 2nd",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:melodic:3rd",
     .display_name = "Treble Melodic 3rd",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:melodic:4th",
     .display_name = "Treble Melodic 4th",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:melodic:5th",
     .display_name = "Treble Melodic 5th",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:melodic:6th",
     .display_name = "Treble Melodic 6th",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Treble},
    {.shape_id = "interval:treble:melodic:octave",
     .display_name = "Treble Melodic Octave",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Treble},

    // 12 Bass intervals (6 harmonic + 6 melodic)
    {.shape_id = "interval:bass:harmonic:2nd",
     .display_name = "Bass Harmonic 2nd",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:harmonic:3rd",
     .display_name = "Bass Harmonic 3rd",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:harmonic:4th",
     .display_name = "Bass Harmonic 4th",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:harmonic:5th",
     .display_name = "Bass Harmonic 5th",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:harmonic:6th",
     .display_name = "Bass Harmonic 6th",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:harmonic:octave",
     .display_name = "Bass Harmonic Octave",
     .kind = FlashcardShapeKind::HarmonicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:melodic:2nd",
     .display_name = "Bass Melodic 2nd",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:melodic:3rd",
     .display_name = "Bass Melodic 3rd",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:melodic:4th",
     .display_name = "Bass Melodic 4th",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:melodic:5th",
     .display_name = "Bass Melodic 5th",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:melodic:6th",
     .display_name = "Bass Melodic 6th",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Bass},
    {.shape_id = "interval:bass:melodic:octave",
     .display_name = "Bass Melodic Octave",
     .kind = FlashcardShapeKind::MelodicInterval,
     .clef = Clef::Bass},

    // 6 Triad inversions
    {.shape_id = "triad:treble:root",
     .display_name = "Treble Triad (Root)",
     .kind = FlashcardShapeKind::TriadInversion,
     .clef = Clef::Treble},
    {.shape_id = "triad:treble:inv1",
     .display_name = "Treble Triad (1st Inv)",
     .kind = FlashcardShapeKind::TriadInversion,
     .clef = Clef::Treble},
    {.shape_id = "triad:treble:inv2",
     .display_name = "Treble Triad (2nd Inv)",
     .kind = FlashcardShapeKind::TriadInversion,
     .clef = Clef::Treble},
    {.shape_id = "triad:bass:root",
     .display_name = "Bass Triad (Root)",
     .kind = FlashcardShapeKind::TriadInversion,
     .clef = Clef::Bass},
    {.shape_id = "triad:bass:inv1",
     .display_name = "Bass Triad (1st Inv)",
     .kind = FlashcardShapeKind::TriadInversion,
     .clef = Clef::Bass},
    {.shape_id = "triad:bass:inv2",
     .display_name = "Bass Triad (2nd Inv)",
     .kind = FlashcardShapeKind::TriadInversion,
     .clef = Clef::Bass},
}};

inline constexpr std::array<FlashcardShapeMeta, 64> kRomanticShapeBank{{
    // Copy of 40 Foundation shapes:
    kShapeBank[0],
    kShapeBank[1],
    kShapeBank[2],
    kShapeBank[3],
    kShapeBank[4],
    kShapeBank[5],
    kShapeBank[6],
    kShapeBank[7],
    kShapeBank[8],
    kShapeBank[9],
    kShapeBank[10],
    kShapeBank[11],
    kShapeBank[12],
    kShapeBank[13],
    kShapeBank[14],
    kShapeBank[15],
    kShapeBank[16],
    kShapeBank[17],
    kShapeBank[18],
    kShapeBank[19],
    kShapeBank[20],
    kShapeBank[21],
    kShapeBank[22],
    kShapeBank[23],
    kShapeBank[24],
    kShapeBank[25],
    kShapeBank[26],
    kShapeBank[27],
    kShapeBank[28],
    kShapeBank[29],
    kShapeBank[30],
    kShapeBank[31],
    kShapeBank[32],
    kShapeBank[33],
    kShapeBank[34],
    kShapeBank[35],
    kShapeBank[36],
    kShapeBank[37],
    kShapeBank[38],
    kShapeBank[39],

    // 8 Seventh Chords (Root & Inversions)
    {.shape_id = "seventh:treble:V7:root",
     .display_name = "Treble V7 (Root)",
     .kind = FlashcardShapeKind::SeventhChord,
     .clef = Clef::Treble},
    {.shape_id = "seventh:treble:V7:inv1",
     .display_name = "Treble V6/5 (1st Inv)",
     .kind = FlashcardShapeKind::SeventhChord,
     .clef = Clef::Treble},
    {.shape_id = "seventh:treble:dim7:root",
     .display_name = "Treble vii\xC2\xB0"
                     "7 (Dim 7th)",
     .kind = FlashcardShapeKind::SeventhChord,
     .clef = Clef::Treble},
    {.shape_id = "seventh:treble:halfdim7:root",
     .display_name = "Treble ii\xC3\xB8"
                     "7 (Half-Dim 7th)",
     .kind = FlashcardShapeKind::SeventhChord,
     .clef = Clef::Treble},
    {.shape_id = "seventh:bass:V7:root",
     .display_name = "Bass V7 (Root)",
     .kind = FlashcardShapeKind::SeventhChord,
     .clef = Clef::Bass},
    {.shape_id = "seventh:bass:V7:inv2",
     .display_name = "Bass V4/3 (2nd Inv)",
     .kind = FlashcardShapeKind::SeventhChord,
     .clef = Clef::Bass},
    {.shape_id = "seventh:bass:dim7:root",
     .display_name = "Bass vii\xC2\xB0"
                     "7 (Dim 7th)",
     .kind = FlashcardShapeKind::SeventhChord,
     .clef = Clef::Bass},
    {.shape_id = "seventh:bass:halfdim7:root",
     .display_name = "Bass ii\xC3\xB8"
                     "7 (Half-Dim 7th)",
     .kind = FlashcardShapeKind::SeventhChord,
     .clef = Clef::Bass},

    // 6 Extreme Ledger-Line Landmarks
    {.shape_id = "ledger:treble:A5",
     .display_name = "Ledger: High A5",
     .kind = FlashcardShapeKind::LedgerLandmark,
     .clef = Clef::Treble},
    {.shape_id = "ledger:treble:D6",
     .display_name = "Ledger: High D6",
     .kind = FlashcardShapeKind::LedgerLandmark,
     .clef = Clef::Treble},
    {.shape_id = "ledger:treble:E6",
     .display_name = "Ledger: High E6",
     .kind = FlashcardShapeKind::LedgerLandmark,
     .clef = Clef::Treble},
    {.shape_id = "ledger:bass:E2",
     .display_name = "Ledger: Low E2",
     .kind = FlashcardShapeKind::LedgerLandmark,
     .clef = Clef::Bass},
    {.shape_id = "ledger:bass:B1",
     .display_name = "Ledger: Low B1",
     .kind = FlashcardShapeKind::LedgerLandmark,
     .clef = Clef::Bass},
    {.shape_id = "ledger:bass:A1",
     .display_name = "Ledger: Low A1",
     .kind = FlashcardShapeKind::LedgerLandmark,
     .clef = Clef::Bass},

    // 6 Chromatic Alterations
    {.shape_id = "chromatic:treble:leading",
     .display_name = "Chromatic: Raised Leading Tone",
     .kind = FlashcardShapeKind::ChromaticAlteration,
     .clef = Clef::Treble},
    {.shape_id = "chromatic:treble:neapolitan",
     .display_name = "Chromatic: Neapolitan bII",
     .kind = FlashcardShapeKind::ChromaticAlteration,
     .clef = Clef::Treble},
    {.shape_id = "chromatic:treble:sec_dom",
     .display_name = "Chromatic: Raised 4th (#4)",
     .kind = FlashcardShapeKind::ChromaticAlteration,
     .clef = Clef::Treble},
    {.shape_id = "chromatic:bass:leading",
     .display_name = "Bass Chromatic: Leading Tone",
     .kind = FlashcardShapeKind::ChromaticAlteration,
     .clef = Clef::Bass},
    {.shape_id = "chromatic:bass:neapolitan",
     .display_name = "Bass Chromatic: Neapolitan bII",
     .kind = FlashcardShapeKind::ChromaticAlteration,
     .clef = Clef::Bass},
    {.shape_id = "chromatic:bass:sec_dom",
     .display_name = "Bass Chromatic: Raised 4th (#4)",
     .kind = FlashcardShapeKind::ChromaticAlteration,
     .clef = Clef::Bass},

    // 4 Two-Hand Vertical Grabs
    {.shape_id = "twohand:tonic:root",
     .display_name = "Grand Staff: Tonic Grab",
     .kind = FlashcardShapeKind::TwoHandGrab,
     .clef = Clef::Treble},
    {.shape_id = "twohand:subdom:root",
     .display_name = "Grand Staff: Subdominant Grab",
     .kind = FlashcardShapeKind::TwoHandGrab,
     .clef = Clef::Treble},
    {.shape_id = "twohand:dominant:V7",
     .display_name = "Grand Staff: Dominant 7th Grab",
     .kind = FlashcardShapeKind::TwoHandGrab,
     .clef = Clef::Treble},
    {.shape_id = "twohand:neapolitan:bII6",
     .display_name = "Grand Staff: Neapolitan bII6 Grab",
     .kind = FlashcardShapeKind::TwoHandGrab,
     .clef = Clef::Treble},
}};

[[nodiscard]] bool matches_category(FlashcardShapeKind kind, FlashcardCategory cat) noexcept {
  switch (cat) {
  case FlashcardCategory::All:
    return true;
  case FlashcardCategory::Landmarks:
    return kind == FlashcardShapeKind::Landmark || kind == FlashcardShapeKind::LedgerLandmark ||
           kind == FlashcardShapeKind::ChromaticAlteration;
  case FlashcardCategory::Intervals:
    return kind == FlashcardShapeKind::HarmonicInterval ||
           kind == FlashcardShapeKind::MelodicInterval;
  case FlashcardCategory::Triads:
    return kind == FlashcardShapeKind::TriadInversion || kind == FlashcardShapeKind::SeventhChord ||
           kind == FlashcardShapeKind::TwoHandGrab;
  }
  return true;
}

[[nodiscard]] SpelledNote spelled_from_step_in_key(int step, KeySignature key) noexcept {
  int octave = step / 7;
  int letter_idx = step % 7;
  if (letter_idx < 0) {
    letter_idx += 7;
    octave -= 1;
  }
  const auto letter = static_cast<NoteLetter>(letter_idx);
  return SpelledNote{
      .letter = letter, .accidental = key_accidental_for(key, letter), .octave = octave};
}

[[nodiscard]] std::string interval_quality_label(int semitones, int diatonic_span) {
  switch (diatonic_span) {
  case 1:
    if (semitones == 1) {
      return "m2";
    }
    if (semitones == 2) {
      return "M2";
    }
    if (semitones == 3) {
      return "A2";
    }
    break;
  case 2:
    if (semitones == 2) {
      return "d3";
    }
    if (semitones == 3) {
      return "m3";
    }
    if (semitones == 4) {
      return "M3";
    }
    break;
  case 3:
    if (semitones == 4) {
      return "d4";
    }
    if (semitones == 5) {
      return "P4";
    }
    if (semitones == 6) {
      return "A4";
    }
    break;
  case 4:
    if (semitones == 6) {
      return "d5";
    }
    if (semitones == 7) {
      return "P5";
    }
    if (semitones == 8) {
      return "A5";
    }
    break;
  case 5:
    if (semitones == 8) {
      return "m6";
    }
    if (semitones == 9) {
      return "M6";
    }
    break;
  case 7:
    if (semitones == 12) {
      return "P8";
    }
    break;
  default:
    break;
  }
  return std::format("{}st", semitones);
}

[[nodiscard]] int interval_span_from_id(std::string_view shape_id) noexcept {
  if (shape_id.ends_with(":2nd")) {
    return 1;
  }
  if (shape_id.ends_with(":3rd")) {
    return 2;
  }
  if (shape_id.ends_with(":4th")) {
    return 3;
  }
  if (shape_id.ends_with(":5th")) {
    return 4;
  }
  if (shape_id.ends_with(":6th")) {
    return 5;
  }
  return 7; // ":octave"
}

[[nodiscard]] std::pair<NoteLetter, int> landmark_from_id(std::string_view shape_id) noexcept {
  if (shape_id.ends_with(":C4")) {
    return {NoteLetter::C, 4};
  }
  if (shape_id.ends_with(":G4")) {
    return {NoteLetter::G, 4};
  }
  if (shape_id.ends_with(":C5")) {
    return {NoteLetter::C, 5};
  }
  if (shape_id.ends_with(":G5")) {
    return {NoteLetter::G, 5};
  }
  if (shape_id.ends_with(":C6")) {
    return {NoteLetter::C, 6};
  }
  if (shape_id.ends_with(":F3")) {
    return {NoteLetter::F, 3};
  }
  if (shape_id.ends_with(":C3")) {
    return {NoteLetter::C, 3};
  }
  if (shape_id.ends_with(":F2")) {
    return {NoteLetter::F, 2};
  }
  if (shape_id.ends_with(":C2")) {
    return {NoteLetter::C, 2};
  }
  return {NoteLetter::C, 4};
}

[[nodiscard]] Inversion inversion_from_id(std::string_view shape_id) noexcept {
  if (shape_id.ends_with(":inv1")) {
    return Inversion::First;
  }
  if (shape_id.ends_with(":inv2")) {
    return Inversion::Second;
  }
  return Inversion::Root;
}

[[nodiscard]] std::string_view inversion_suffix(Inversion inv) noexcept {
  switch (inv) {
  case Inversion::Root:
    return "Root";
  case Inversion::First:
    return "1st Inv";
  case Inversion::Second:
    return "2nd Inv";
  }
  return "Root";
}

} // namespace

double compute_shape_weight(const ShapeTelemetry& telemetry) noexcept {
  if (telemetry.attempts == 0) {
    return 2.0;
  }
  double weight = 1.0;
  const double acc = telemetry.accuracy();
  if (acc < 0.85) {
    weight += 4.0 + 6.0 * (0.85 - acc);
  }
  if (telemetry.ema_reaction_ms > 1500.0) {
    const double over_sec = std::min(2.5, (telemetry.ema_reaction_ms - 1500.0) / 1000.0);
    weight += 4.0 + 4.0 * over_sec;
  }
  return weight;
}

std::span<const FlashcardShapeMeta> flashcard_shape_bank() noexcept {
  return kShapeBank;
}

std::span<const FlashcardShapeMeta> flashcard_shape_bank(FlashcardLevel level) noexcept {
  return (level == FlashcardLevel::Romantic)
             ? std::span<const FlashcardShapeMeta>(kRomanticShapeBank)
             : std::span<const FlashcardShapeMeta>(kShapeBank);
}

FlashcardEngine::FlashcardEngine(KeySignature key, std::uint32_t seed, double initial_now_ms,
                                 MinorScaleMode minor_mode)
    : key_{key}, minor_mode_{minor_mode}, rng_{seed}, prompt_shown_ms_{initial_now_ms} {
  next_card(initial_now_ms);
}

void FlashcardEngine::set_key(KeySignature key, MinorScaleMode minor_mode, double now_ms) {
  key_ = key;
  minor_mode_ = minor_mode;
  next_card(now_ms);
}

void FlashcardEngine::set_category(FlashcardCategory category, double now_ms) {
  category_ = category;
  next_card(now_ms);
}

void FlashcardEngine::set_level(FlashcardLevel level, double now_ms) {
  if (level_ == level) {
    return;
  }
  level_ = level;
  next_card(now_ms);
}

void FlashcardEngine::next_card(double now_ms) {
  const auto active_bank = flashcard_shape_bank(level_);
  std::vector<const FlashcardShapeMeta*> candidates;
  std::vector<double> weights;
  candidates.reserve(active_bank.size());
  weights.reserve(active_bank.size());

  for (const auto& meta : active_bank) {
    if (!matches_category(meta.kind, category_)) {
      continue;
    }
    candidates.push_back(&meta);
    double w = 2.0;
    if (const auto it = telemetry_.find(std::string(meta.shape_id)); it != telemetry_.end()) {
      w = compute_shape_weight(it->second);
    }
    weights.push_back(w);
  }

  if (candidates.empty()) {
    candidates.push_back(&kShapeBank[0]);
    weights.push_back(1.0);
  }

  if (candidates.size() >= 2 && !last_shape_id_.empty()) {
    std::vector<double> filtered = weights;
    double total = 0.0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      if (candidates[i]->shape_id == last_shape_id_) {
        filtered[i] = 0.0;
      }
      total += filtered[i];
    }
    if (total > 0.0) {
      weights = std::move(filtered);
    }
  }

  std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
  const FlashcardShapeMeta* chosen = candidates[dist(rng_)];

  current_prompt_ = instantiate_shape(*chosen);
  last_shape_id_ = current_prompt_.shape_id;
  substep_index_ = 0;
  prompt_shown_ms_ = now_ms;
  card_solved_at_ms_ = -1.0;
  card_solved_ = false;
  card_had_error_ = false;
  card_recorded_ = false;
  last_played_midi_.clear();
}

bool FlashcardEngine::update(double now_ms) {
  if (card_solved_ && auto_advance_delay_ms_ >= 0.0 && card_solved_at_ms_ >= 0.0 &&
      (now_ms - card_solved_at_ms_) >= auto_advance_delay_ms_) {
    next_card(now_ms);
    return true;
  }
  return false;
}

FlashcardEvalResult FlashcardEngine::submit_played_notes(std::span<const MidiPitch> played_midi,
                                                         double now_ms) {
  auto normalized = normalize_midi_set(played_midi);
  if (normalized.empty() || card_solved_ || substep_index_ >= current_prompt_.steps.size()) {
    return FlashcardEvalResult{};
  }

  last_played_midi_ = normalized;
  const auto& step = current_prompt_.steps[substep_index_];
  auto& stats = telemetry_[current_prompt_.shape_id];

  if (normalized == step.expected_midi) {
    ++substep_index_;
    if (substep_index_ < current_prompt_.steps.size()) {
      return FlashcardEvalResult{
          .status = FlashcardEvalStatus::PartialProgress,
          .reaction_ms = std::clamp(now_ms - prompt_shown_ms_, 50.0, 15000.0),
          .expected_midi = step.expected_midi,
          .played_midi = std::move(normalized),
      };
    }

    const double rt_ms = std::clamp(now_ms - prompt_shown_ms_, 50.0, 15000.0);
    if (!card_recorded_) {
      ++stats.attempts;
      ++stats.correct;
      ++session_attempts_;
      ++session_correct_;
      card_recorded_ = true;
    }

    if (stats.ema_reaction_ms <= 0.0) {
      stats.ema_reaction_ms = rt_ms;
    } else {
      stats.ema_reaction_ms = 0.3 * rt_ms + 0.7 * stats.ema_reaction_ms;
    }

    session_rt_sum_ms_ += rt_ms;
    ++session_rt_count_;

    card_solved_ = true;
    card_solved_at_ms_ = now_ms;

    return FlashcardEvalResult{
        .status = FlashcardEvalStatus::Solved,
        .reaction_ms = rt_ms,
        .expected_midi = step.expected_midi,
        .played_midi = std::move(normalized),
    };
  }

  card_had_error_ = true;
  if (!card_recorded_) {
    ++stats.attempts;
    ++session_attempts_;
    card_recorded_ = true;
  }

  return FlashcardEvalResult{
      .status = FlashcardEvalStatus::Incorrect,
      .reaction_ms = std::clamp(now_ms - prompt_shown_ms_, 50.0, 15000.0),
      .expected_midi = step.expected_midi,
      .played_midi = std::move(normalized),
  };
}

void FlashcardEngine::load_telemetry(std::map<std::string, ShapeTelemetry> telemetry) {
  telemetry_ = std::move(telemetry);
}

std::vector<WeakShapeSummary> FlashcardEngine::weakest_shapes(std::size_t max_count) const {
  const auto active_bank = flashcard_shape_bank(level_);
  std::vector<WeakShapeSummary> list;
  list.reserve(active_bank.size());
  for (const auto& meta : active_bank) {
    ShapeTelemetry tel{};
    if (const auto it = telemetry_.find(std::string(meta.shape_id)); it != telemetry_.end()) {
      tel = it->second;
    }
    list.push_back(WeakShapeSummary{
        .shape_id = std::string(meta.shape_id),
        .display_name = std::string(meta.display_name),
        .telemetry = tel,
        .weight = compute_shape_weight(tel),
    });
  }

  std::ranges::sort(list, [](const WeakShapeSummary& a, const WeakShapeSummary& b) {
    if (a.weight != b.weight) {
      return a.weight > b.weight;
    }
    if ((a.telemetry.attempts > 0) != (b.telemetry.attempts > 0)) {
      return a.telemetry.attempts > b.telemetry.attempts;
    }
    if (a.telemetry.accuracy() != b.telemetry.accuracy()) {
      return a.telemetry.accuracy() < b.telemetry.accuracy();
    }
    if (a.telemetry.ema_reaction_ms != b.telemetry.ema_reaction_ms) {
      return a.telemetry.ema_reaction_ms > b.telemetry.ema_reaction_ms;
    }
    return a.shape_id < b.shape_id;
  });

  if (list.size() > max_count) {
    list.resize(max_count);
  }
  return list;
}

std::size_t FlashcardEngine::expected_cardinality() const noexcept {
  if (card_solved_ || substep_index_ >= current_prompt_.steps.size()) {
    return 1;
  }
  const std::size_t sz = current_prompt_.steps[substep_index_].expected_midi.size();
  return sz > 0 ? sz : 1;
}

double FlashcardEngine::session_accuracy() const noexcept {
  if (session_attempts_ == 0) {
    return 0.0;
  }
  return static_cast<double>(session_correct_) / static_cast<double>(session_attempts_);
}

double FlashcardEngine::session_avg_reaction_ms() const noexcept {
  if (session_rt_count_ == 0) {
    return 0.0;
  }
  return session_rt_sum_ms_ / static_cast<double>(session_rt_count_);
}

FlashcardPrompt FlashcardEngine::instantiate_shape(const FlashcardShapeMeta& meta) {
  FlashcardPrompt prompt{};
  prompt.shape_id = std::string(meta.shape_id);
  prompt.display_name = std::string(meta.display_name);
  prompt.kind = meta.kind;
  prompt.clef = meta.clef;

  if (meta.kind == FlashcardShapeKind::Landmark) {
    const auto [letter, octave] = landmark_from_id(meta.shape_id);
    const SpelledNote note{
        .letter = letter, .accidental = key_accidental_for(key_, letter), .octave = octave};
    const auto midi = to_midi(note);
    const MidiPitch m = midi.has_value() ? *midi : kMiddleCMidi;
    prompt.detail_label = format_note(note);
    prompt.steps.push_back(FlashcardStep{
        .notes = {note},
        .expected_midi = {m},
        .clef = meta.clef,
    });
    return prompt;
  }

  if (meta.kind == FlashcardShapeKind::HarmonicInterval ||
      meta.kind == FlashcardShapeKind::MelodicInterval) {
    const int span = interval_span_from_id(meta.shape_id);
    const int min_step = (meta.clef == Clef::Treble) ? 28 : 16; // C4 / E2
    const int max_step = (meta.clef == Clef::Treble) ? 40 : 28; // A5 / C4

    std::vector<std::pair<SpelledNote, SpelledNote>> pairs;
    for (int s = min_step; s + span <= max_step; ++s) {
      const SpelledNote lo = spelled_from_step_in_key(s, key_);
      const SpelledNote hi = spelled_from_step_in_key(s + span, key_);
      const auto m_lo = to_midi(lo);
      const auto m_hi = to_midi(hi);
      if (m_lo.has_value() && m_hi.has_value() && *m_lo >= kPianoLowestMidi &&
          *m_hi <= kPianoHighestMidi && *m_hi > *m_lo) {
        pairs.emplace_back(lo, hi);
      }
    }

    if (pairs.empty()) {
      const SpelledNote lo{.letter = NoteLetter::C,
                           .accidental = Accidental::Natural,
                           .octave = meta.clef == Clef::Treble ? 4 : 3};
      const SpelledNote hi = spelled_from_step_in_key(diatonic_step(lo) + span, key_);
      pairs.emplace_back(lo, hi);
    }

    std::uniform_int_distribution<std::size_t> dist(0, pairs.size() - 1);
    const auto [lo, hi] = pairs[dist(rng_)];
    const MidiPitch m_lo = to_midi(lo).value_or(kMiddleCMidi);
    const MidiPitch m_hi = to_midi(hi).value_or(kMiddleCMidi);
    const int semitones = static_cast<int>(m_hi) - static_cast<int>(m_lo);
    const std::string iv_lbl = interval_quality_label(semitones, span);

    if (meta.kind == FlashcardShapeKind::HarmonicInterval) {
      prompt.detail_label = std::format("{} ({} + {})", iv_lbl, format_note(lo), format_note(hi));
      prompt.steps.push_back(FlashcardStep{
          .notes = {lo, hi},
          .expected_midi = normalize_midi_set(std::array<MidiPitch, 2>{m_lo, m_hi}),
          .clef = meta.clef,
      });
    } else {
      std::uniform_int_distribution<int> dir_dist(0, 1);
      const bool ascending = (dir_dist(rng_) == 0);
      const SpelledNote& first = ascending ? lo : hi;
      const SpelledNote& second = ascending ? hi : lo;
      const MidiPitch m_first = ascending ? m_lo : m_hi;
      const MidiPitch m_second = ascending ? m_hi : m_lo;
      prompt.detail_label =
          std::format("{} ({} -> {})", iv_lbl, format_note(first), format_note(second));
      prompt.steps.push_back(FlashcardStep{
          .notes = {first},
          .expected_midi = {m_first},
          .clef = meta.clef,
      });
      prompt.steps.push_back(FlashcardStep{
          .notes = {second},
          .expected_midi = {m_second},
          .clef = meta.clef,
      });
    }
    return prompt;
  }

  if (meta.kind == FlashcardShapeKind::LedgerLandmark) {
    NoteLetter letter = NoteLetter::A;
    int octave = 5;
    if (meta.shape_id.ends_with(":A5")) {
      letter = NoteLetter::A;
      octave = 5;
    } else if (meta.shape_id.ends_with(":D6")) {
      letter = NoteLetter::D;
      octave = 6;
    } else if (meta.shape_id.ends_with(":E6")) {
      letter = NoteLetter::E;
      octave = 6;
    } else if (meta.shape_id.ends_with(":E2")) {
      letter = NoteLetter::E;
      octave = 2;
    } else if (meta.shape_id.ends_with(":B1")) {
      letter = NoteLetter::B;
      octave = 1;
    } else if (meta.shape_id.ends_with(":A1")) {
      letter = NoteLetter::A;
      octave = 1;
    }
    const SpelledNote note{
        .letter = letter, .accidental = key_accidental_for(key_, letter), .octave = octave};
    const MidiPitch m = to_midi(note).value_or(kMiddleCMidi);
    prompt.detail_label = format_note(note);
    prompt.steps.push_back(FlashcardStep{.notes = {note}, .expected_midi = {m}, .clef = meta.clef});
    return prompt;
  }

  if (meta.kind == FlashcardShapeKind::ChromaticAlteration) {
    const int oct = (meta.clef == Clef::Treble) ? 4 : 3;
    const auto t = tonic_of(key_);
    int target_midi =
        static_cast<int>(to_midi(SpelledNote{t.letter, t.accidental, oct}).value_or(60));
    std::string tag;
    if (meta.shape_id.ends_with(":leading")) {
      target_midi += 11;
      tag = "Leading tone";
    } else if (meta.shape_id.ends_with(":neapolitan")) {
      target_midi += 1;
      tag = "bII";
    } else {
      target_midi += 6;
      tag = "#4";
    }
    if (meta.clef == Clef::Treble && target_midi < 60) {
      target_midi += 12;
    }
    if (meta.clef == Clef::Bass && target_midi > 58) {
      target_midi -= 12;
    }
    const MidiPitch m = static_cast<MidiPitch>(std::clamp(target_midi, 36, 84));
    const SpelledNote sn =
        spell_midi_in_key(m, key_).value_or(SpelledNote{t.letter, t.accidental, oct});
    prompt.detail_label = std::format("{} ({})", format_note(sn), tag);
    prompt.steps.push_back(FlashcardStep{.notes = {sn}, .expected_midi = {m}, .clef = meta.clef});
    return prompt;
  }

  if (meta.kind == FlashcardShapeKind::SeventhChord) {
    const int base_oct = (meta.clef == Clef::Treble) ? 4 : 2;
    const auto t = tonic_of(key_);
    int root_m =
        static_cast<int>(to_midi(SpelledNote{t.letter, t.accidental, base_oct}).value_or(60));
    std::array<int, 4> ivs{0, 4, 7, 10};
    std::string chord_tag = "V7";
    if (meta.shape_id.find(":V7:") != std::string_view::npos) {
      root_m += 7;
      if (meta.shape_id.ends_with(":inv1")) {
        ivs = {4, 7, 10, 12};
        chord_tag = "V6/5";
      } else if (meta.shape_id.ends_with(":inv2")) {
        ivs = {7, 10, 12, 16};
        chord_tag = "V4/3";
      }
    } else if (meta.shape_id.find(":dim7:") != std::string_view::npos) {
      root_m -= 1;
      ivs = {0, 3, 6, 9};
      chord_tag = "vii\xC2\xB0"
                  "7";
    } else {
      root_m += 2;
      ivs = {0, 3, 6, 10};
      chord_tag = "ii\xC3\xB8"
                  "7";
    }
    if (meta.clef == Clef::Treble && root_m > 68) {
      root_m -= 12;
    }
    if (meta.clef == Clef::Bass && root_m < 40) {
      root_m += 12;
    }
    std::vector<SpelledNote> notes;
    std::vector<MidiPitch> expected;
    for (const int d : ivs) {
      const MidiPitch m = static_cast<MidiPitch>(std::clamp(root_m + d, 36, 84));
      expected.push_back(m);
      notes.push_back(spell_midi_in_key(m, key_).value_or(SpelledNote{}));
    }
    prompt.detail_label = chord_tag;
    prompt.steps.push_back(FlashcardStep{.notes = std::move(notes),
                                         .expected_midi = normalize_midi_set(expected),
                                         .clef = meta.clef});
    return prompt;
  }

  if (meta.kind == FlashcardShapeKind::TwoHandGrab) {
    const auto t = tonic_of(key_);
    const int rh_tonic_m =
        static_cast<int>(to_midi(SpelledNote{t.letter, t.accidental, 4}).value_or(60));
    int bass_m = rh_tonic_m - 12;
    std::array<int, 3> rh_ivs{0, (key_.mode == Mode::Minor) ? 3 : 4, 7};
    int rh_root = rh_tonic_m;
    std::string tag = "I";
    if (meta.shape_id.find(":subdom:") != std::string_view::npos) {
      bass_m = rh_tonic_m - 12 + 5;
      rh_root = rh_tonic_m + 5;
      rh_ivs = {0, (key_.mode == Mode::Minor) ? 3 : 4, 7};
      tag = (key_.mode == Mode::Minor) ? "iv" : "IV";
    } else if (meta.shape_id.find(":dominant:") != std::string_view::npos) {
      bass_m = rh_tonic_m - 12 + 7;
      rh_root = rh_tonic_m + 7;
      rh_ivs = {0, 4, 10};
      tag = "V7";
    } else if (meta.shape_id.find(":neapolitan:") != std::string_view::npos) {
      bass_m = rh_tonic_m - 12 + 5;
      rh_root = rh_tonic_m + 1;
      rh_ivs = {0, 4, 7};
      tag = "bII6";
    }
    if (bass_m > 55) {
      bass_m -= 12;
    }
    if (rh_root > 68) {
      rh_root -= 12;
    }
    std::vector<SpelledNote> notes;
    std::vector<Clef> clefs;
    std::vector<MidiPitch> expected;
    const MidiPitch bm = static_cast<MidiPitch>(std::clamp(bass_m, 36, 56));
    notes.push_back(spell_midi_in_key(bm, key_).value_or(SpelledNote{}));
    clefs.push_back(Clef::Bass);
    expected.push_back(bm);
    for (const int d : rh_ivs) {
      const MidiPitch rm = static_cast<MidiPitch>(std::clamp(rh_root + d, 58, 84));
      notes.push_back(spell_midi_in_key(rm, key_).value_or(SpelledNote{}));
      clefs.push_back(Clef::Treble);
      expected.push_back(rm);
    }
    prompt.detail_label = std::format("{} ({})", tag, format_key_name(key_));
    prompt.steps.push_back(FlashcardStep{.notes = std::move(notes),
                                         .expected_midi = normalize_midi_set(expected),
                                         .clef = Clef::Treble,
                                         .note_clefs = std::move(clefs)});
    return prompt;
  }

  // TriadInversion
  const Inversion inv = inversion_from_id(meta.shape_id);
  const int min_step = (meta.clef == Clef::Treble) ? 28 : 15; // C4..B5 / D2..C4
  const int max_step = (meta.clef == Clef::Treble) ? 41 : 28;
  const std::array<int, 3> octaves =
      (meta.clef == Clef::Treble) ? std::array<int, 3>{3, 4, 5} : std::array<int, 3>{1, 2, 3};
  const bool harmonic_dom = (key_.mode == Mode::Minor && minor_mode_ == MinorScaleMode::Harmonic);

  std::vector<Triad> triads;
  for (const int deg : {1, 4, 5, 2, 6}) {
    for (const int oct : octaves) {
      const auto t = build_diatonic_triad(key_, deg, oct, inv, harmonic_dom);
      if (!t.has_value()) {
        continue;
      }
      bool in_bounds = true;
      for (const auto& n : t->notes) {
        const int st = diatonic_step(n);
        const auto m = to_midi(n);
        if (st < min_step || st > max_step || !m.has_value() || *m < kPianoLowestMidi ||
            *m > kPianoHighestMidi) {
          in_bounds = false;
          break;
        }
      }
      if (in_bounds) {
        triads.push_back(*t);
      }
    }
  }

  if (triads.empty()) {
    const int fallback_oct = (meta.clef == Clef::Treble) ? 4 : 2;
    if (const auto t = build_diatonic_triad(key_, 1, fallback_oct, inv, harmonic_dom);
        t.has_value()) {
      triads.push_back(*t);
    }
  }

  std::uniform_int_distribution<std::size_t> t_dist(0, triads.size() - 1);
  const auto& chosen_triad = triads[t_dist(rng_)];
  std::vector<SpelledNote> notes(chosen_triad.notes.begin(), chosen_triad.notes.end());
  std::vector<MidiPitch> expected;
  expected.reserve(notes.size());
  for (const auto& n : notes) {
    expected.push_back(to_midi(n).value_or(kMiddleCMidi));
  }
  expected = normalize_midi_set(expected);

  prompt.detail_label =
      std::format("{} ({})", format_chord_symbol(chosen_triad), inversion_suffix(inv));
  prompt.steps.push_back(FlashcardStep{
      .notes = std::move(notes),
      .expected_midi = std::move(expected),
      .clef = meta.clef,
  });
  return prompt;
}

} // namespace selfano::core
