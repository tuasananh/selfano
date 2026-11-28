# Phase 29: MIDI Evenness, Thumb-Crossing & Dynamic Contour Coaching

**Status:** `[x] Complete` (Depends on Phase 16 & Phase 28 — Track C: Daily Drills & MIDI Technique Diagnostics)  
**Goal:** Deepen Phase 16's pianistic coaching by adding **IOI rhythmic evenness diagnostics** on fast runs, **thumb-crossing / velocity-spike detection**, **dynamic contour compliance (`pp`–`ff` & hairpins)**, and **explicit melody-voice awareness** (supporting Left-Hand and inner-voice melodies instead of assuming top treble note).

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `apple-hig-imgui`, `verification-and-review`

---

## 1. Why This Matters Pedagogically

MIDI cannot see a stiff wrist, *but it can hear one*:
- In scales and 16th-note runs, weak fingers (`4–5`) rush or drag, showing up directly as **inter-onset interval (IOI) jitter**.
- Awkward thumb-under crossings (`3 -> 1` or `4 -> 1`) produce sudden **+18–30 velocity spikes** that destroy Chopin's pearly legato.
- Flat dynamics (`mf` everywhere) drain Romantic pieces of life, and `LessonEngine::submit_played_notes` currently hardcodes `tgt.treble_midi.back()` as the melody—breaking whenever a melody sits in the Left Hand or an inner voice.

---

## 2. Deliverables & Vertical Slices

### Slice 29.1 — Melody-Voice Tagging & Expanded `MusicalityMetrics` (`src/core/lesson_score.hpp`, `src/core/lesson_engine.hpp/.cpp`)
- Add optional `bool is_melody{false}` on `LessonNote` (or `LessonEvent`) with backward-compatible default fallback (`treble_midi.back()` only when no explicit `is_melody` tags exist in the measure), enabling accurate voicing analysis when the melody sits in the Left Hand or inner alto/tenor voice.
- Extend `MusicalityMetrics` (keeping all metrics **non-punitive** and gated cleanly on hardware velocity / `SteadyPulse` where appropriate):
  - **Rhythmic Evenness (`evenness_available`, `ioi_evenness_score` `0..1`):** Measure coefficient of variation across consecutive equal-subdivision `8th`/`16th` runs (`>= 4` consecutive notes).
  - **Velocity Smoothness / Thumb-Spike Check (`vel_smoothness_available`, `thumb_spike_count`):** Flag isolated velocity outliers (`> +20` above rolling local average on unaccented non-melody/scale notes, especially where `finger == 1` follows `finger == 3` or `4`).
  - **Dynamic Contrast (`dynamics_available`, `dynamic_contrast_ok`):** On scores containing both soft (`pp`/`p`) and loud (`f`/`ff`) sections, verify hardware mean velocity in `f/ff` exceeds `pp/p` by at least `+14` vel.
- Generate concise, prioritized `coach_tip` strings (at most 2 tips so the coach never overwhelms the learner).
- Add Catch2 unit tests in `tests/lesson_engine_tests.cpp` covering uneven 16ths, thumb velocity spikes, dynamic `pp` vs. `ff` contrast, LH melody voicing, and virtual-keyboard graceful omission.

### Slice 29.2 — Warm-Up Evenness & Thumb-Crossing Feedback (`src/core/warmup_engine.hpp/.cpp`, `src/ui/warmup_view.cpp`)
- Extend `WarmUpEngine::submit_played_notes(...)` with optional velocity input so Guided Pulse scale/arpeggio passes report **Rhythm Evenness %** and **Tone Smoothness** (catching thumb-crossing thumps in real time during the 5-minute daily warm-up).

### Slice 29.3 — Refined Musicality Row in Post-Run Coach UI (`src/ui/lesson_view.cpp`, `src/app/cli_options.hpp/.cpp`)
- Surface the new non-punitive pills (`Evenness`, `Dynamics`, `Smoothness`) concisely inside `LessonView::draw_post_run_coach` alongside `Voicing`, `Sustain`, and `Pedal` (showing only metrics relevant to that run so the card stays airy and scannable).

---

## 3. Exit Gate (Verification Criteria)

1. `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiles with **0 warnings** under `-Wall -Wextra -Wpedantic` and passes 100% of Catch2 tests.
2. `emcmake cmake --preset web-release && cmake --build --preset web-release` compiles cleanly with **0 warnings**.
3. `src/core/` purity check returns 0 matches; `clang-format --dry-run --Werror` clean.
4. Headless Light & Dark screenshots (`--simulate-coach clean`, `--simulate-coach imperfect`) confirm crisp, non-crowded Musicality Coach metrics across Lessons and Warm-Up.
