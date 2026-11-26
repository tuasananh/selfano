# Phase 28: Advanced Warm-Up Circuits & Printed Scale Fingerings (Stage 1)

**Status:** `[ ] Planned` (Depends on Phase 20 — Track C: Daily Drills & MIDI Technique Diagnostics)  
**Goal:** Elevate Stage 1 (`WarmUpEngine`) from beginner scales/triads into a conservatory technical warm-up by adding **standard printed scale & arpeggio fingerings** for all 24 keys plus selectable intermediate/Chopin warm-up stations: **Chromatic Scale**, **Dominant 7th & Diminished 7th Arpeggios**, **Contrary Motion**, and **LH Stride Jumps**.

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `music-notation-imgui`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

Self-taught pianists who invent ad-hoc scale and arpeggio fingerings hit an unbreakable speed ceiling around `100 BPM`. Meanwhile, Chopin's *Ballade No. 1* relies heavily on four mechanics absent from the current Stage 1 warm-up: **chromatic scales**, **diminished-7th / dominant-7th arpeggios**, **contrary-motion hand independence**, and **blind left-hand stride leaps** (low bass octave ↔ mid-register chord). Embedding these directly into the 5-minute daily warm-up builds Chopin's exact muscle vocabulary effortlessly over months.

---

## 2. Deliverables & Vertical Slices

### Slice 28.1 — `constexpr` 24-Key Fingering Tables & New Warm-Up Stations (`src/core/warmup_engine.hpp/.cpp`, `src/core/theory.hpp/.cpp`)
- Add `constexpr` standard classical fingering tables (ABRSM/Chopin-friendly `1–5` patterns, starting thumb/finger groups for all 12 major and 12 minor keys, both RH and LH, ascending & descending, 1–2 octaves) and attach `std::uint8_t finger{0}` to `WarmUpNote`.
- Add new `WarmUpStation` variants and selectable circuit presets (`FoundationCircuit` = existing 4 stations; `RomanticCircuit` = Parallel Scale + Chromatic + Dom7/Dim7 Arpeggios + LH Stride Jumps; plus individual station picks including `ContraryScale`):
  - **Chromatic Scale:** 1–2 octaves starting on the weekly tonic with standard `1–3 / 1–2–3` fingerings.
  - **Dom7 & Dim7 Arpeggios:** `V7` and `vii°7` of the active weekly key up & down 1–2 octaves.
  - **LH Stride Jumps:** Bass root/octave → mid-register tonic/subdominant/dominant chord → bass root/octave in `3/4` or `6/4` lilt, training blind left-hand leap accuracy.
  - **Contrary Motion Scale:** Symmetric outward-and-inward scale motion from tonic.
- Persist selected warm-up circuit preset cleanly in `profile.json` v1 (`profile_store.hpp/.cpp`).
- Expand `tests/warmup_tests.cpp` verifying fingering bounds (`1..5`), no thumb-on-black-key errors in standard C/G/D/A/E/F/Bb/Eb/Ab scales, exact MIDI pitches for chromatic/Dom7/Dim7/stride stations across all 24 keys.

### Slice 28.2 — Fingering Engraving & Circuit Picker in `WarmUpView` (`src/ui/warmup_view.hpp/.cpp`, `src/ui/staff_renderer.cpp`)
- Pass `WarmUpNote::finger` to `StaffRenderer` so printed scale/arpeggio fingerings appear cleanly above/below notes (with a `Show fingerings` toggle in Warm-Up Options, on by default).
- Add a clean `Foundation | Romantic` circuit selector in `WarmUpView` options and display concise station pills matching the active circuit.

### Slice 28.3 — Verification & Polish (`src/app/app_controller.cpp`, `tests/`)
- Ensure Guided Pulse BPM ladder, per-key `warmup_key_bpm` persistence, and Autopilot safe station boundaries (`is_mid_station()`) work identically across all new stations.

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests across all 24 keys and new warm-up stations.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots of `--tab warmup` in `G Minor` verify crisp printed fingerings, Chromatic / Dim7 / LH Stride engraving, and clean single-row controls.
