---
name: cpp23-tdd
description: >-
  Enforces strict Red-Green-Refactor Test-Driven Development in C++23 with
  Catch2 and ctest for piano_core and testable piano_midi components. Use
  whenever implementing new theory, generator, matcher, SRS, persistence, or
  lock-free queue functionality, or fixing logic bugs.
---

# C++23 Test-Driven Development (Catch2 + ctest)

Adapted from Superpowers (`test-driven-development`) and Matt Pocock (`tdd`) for modern C++23.

## Hard Rules

1. **No production logic without a failing test first** for everything in `src/core/` and deterministic `src/midi/` components (`SpscQueue`, `PolyphonicChordGrouper`, timing/state helpers).
2. **You MUST observe the Red state.** Compile and run `ctest` after writing the test to confirm it fails for the expected reason (missing behavior or assertion failure), not because of an unrelated typo.
3. **Write minimal Green code.** Resist speculative abstractions (YAGNI). Satisfy the test cleanly with warning-free C++23.
4. **Refactor only on Green.** Keep `ctest` passing after every cleanup step.

## The Red-Green-Refactor Loop

### Step 1 — Red (Write One Focused Test)
- Add a `TEST_CASE` / `SECTION` in `tests/` testing observable behavior through the public header API (not private internals).
- Cover edge cases explicitly:
  - Music theory: enharmonic spellings (`F#` vs `Gb`, `Cb`/`B#`), octave boundaries (A0–C8), ledger lines, key signature accidentals.
  - Generators: fixed-seed determinism, key-signature compliance, range bounds.
  - Matchers & SRS: chord inversions, passing tones, rolling time windows (`now_ms`), SM-2 ease-factor floor (`1.3`).
  - SPSC queue: empty pop, full push, wrap-around across capacity boundary.
- Build and run the targeted test:
  ```bash
  cmake --build --preset native-debug --target selfano_tests && ctest --preset native-debug --output-on-failure
  ```
- Confirm it fails as expected.

### Step 2 — Green (Minimal Warning-Free Implementation)
- Implement the minimal C++23 code in `src/core/` (or `src/midi/`).
- Enforce `AGENTS.md` standards:
  - Zero warnings under `-Wall -Wextra -Wpedantic`.
  - No raw owning `new`/`delete`.
  - Use `std::expected`, `std::span`, `std::array`, `constexpr` where appropriate.
  - Inject time/RNG rather than calling global clocks or `std::random_device`.
- Re-run build + `ctest` and confirm all tests pass.

### Step 3 — Refactor
- Remove duplication, tighten `const`/`constexpr`/`[[nodiscard]]`, improve naming.
- Re-run build + full `ctest` suite to prove zero regressions.

## Exceptions (When TDD Does Not Apply Directly)

- Thin SDL3/ImGui window boilerplate and pure visual layout (`src/ui/`, `src/main.cpp`) are verified via build + runtime visual inspection / `chrome-devtools` rather than unit tests.
- However, **all layout math** (diatonic staff-step mapping, ledger-line count, 2nd-interval horizontal offset calculation, key-signature slot positions) MUST live in pure testable functions and follow this TDD loop.
