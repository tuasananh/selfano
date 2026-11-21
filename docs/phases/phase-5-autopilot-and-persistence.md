# Phase 5: Unified 30-Minute Autopilot Session & `profile.json` Persistence

**Goal:** Deliver the zero-decision-fatigue **"Start Today's Practice"** autopilot experience chaining all 4 stages seamlessly, backed by durable offline `profile.json` persistence (`nlohmann/json`).

**Required Skills:** `spec-phase-workflow`, `cpp23-tdd`, `verification-and-review`

---

## Deliverables & Vertical Slices

### Slice 5.1 — Autopilot Session State Machine (`src/core/autopilot.*` via `cpp23-tdd`)
- Deterministic session orchestrator sequencing the full 30-minute split:
  1. Stage 1 — Key & Harmony Warm-Up (`05:00`)
  2. Stage 2A — Adaptive Flashcards (`03:00`)
  3. Stage 2B — Procedural Continuous Etudes (`07:00`)
  4. Stage 3 — Repertoire Harmonic Mapping / Companion Mode (`10:00`)
  5. Stage 4A — Retention SRS Deck (`02:30`)
  6. Stage 4B — Procedural Ear Check (`02:30`)
- Supports:
  - Automatic stage transition when each block's timer elapses (with brief clean transition banner, never interrupting mid-bar abruptly).
  - Manual **Skip**, **Repeat Block**, **Pause/Resume**, and **End Early** controls.
  - Standalone Free-Practice access to any individual stage from the Home screen.
  - Session summary metrics (minutes practiced, flashcard accuracy/RT delta, etude accuracy, Companion reps, SRS cards reviewed, ear melodies solved).

### Slice 5.2 — Versioned `profile.json` Persistence (`src/core/profile_store.*` via `cpp23-tdd`)
- `UserProfile` schema (`schema_version: 1`) containing:
  - Weekly key index + week start date (auto-advances weekly with manual override option)
  - Flashcard per-shape accuracy & EMA reaction-time telemetry
  - Repertoire pieces & 4-bar chord progression roadmaps (pre-seeded with sensible editable examples on first run if empty)
  - SRS retention deck cards
  - Daily practice history log (`date`, `duration_sec`, `key`, `summary`)
  - User preferences (default BPM, chord window ms, preferred MIDI IN/OUT names, hand mode, metronome enabled)
- Pure JSON serialize/deserialize functions (`to_json_string` / `from_json_string` returning `std::expected<UserProfile, std::string>`) thoroughly unit-tested with `Catch2` (round-trip, missing optional fields, corrupt JSON handling).
- Atomic desktop file save (write `.tmp` + rename) using `SDL_GetPrefPath("selfano", "selfano")`, overridable via CLI `--profile <path>`.

### Slice 5.3 — Home Dashboard & Autopilot Runner Shell (`src/ui/`, `src/main.cpp`)
- Clean Home screen featuring:
  - Prominent **"Start Today's Practice (30:00)"** primary button
  - Active weekly key card (with manual key-picker override)
  - Due SRS card count & active repertoire chunk preview
  - Practice streak & recent telemetry snapshot (average flashcard RT vs. `1500 ms` goal, weakest interval/triad shapes)
  - Quick-launch buttons for individual stages
- Persistent Autopilot top bar during sessions: active stage indicator (`1/4`..`4/4`), countdown timer, Pause / Skip / Repeat controls, and end-of-session recap modal that auto-saves `profile.json`.

---

## Exit Gate (Verification Criteria)

1. Warning-free build and 100% passing `ctest` covering Autopilot stage transitions (auto-advance, pause, skip, repeat, end early) and `profile.json` JSON round-trip / malformed-input recovery.
2. `src/core/` purity check passes.
3. End-to-end manual test with `--profile /tmp/selfano_test.json`: run through all 4 stages via Skip/play, close app, relaunch, and verify telemetry, custom repertoire roadmaps, SRS due dates, and preferences persist cleanly.
