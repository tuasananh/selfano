# `src/core/` (`piano_core`) Boundary Rules

Everything in `src/core/` is pure, deterministic, portable **C++23** business logic.

## Strict Prohibitions

1. **Zero UI / Platform / Hardware Includes:**
   - NEVER `#include <imgui.h>`, `<SDL3/...>`, `<SDL.h>`, `<libremidi/...>`, or `<emscripten.h>`.
   - Allowed external dependencies: C++23 standard library and `nlohmann/json` (for persistence serialization only, isolated in `profile_store.*`).
2. **No Hidden Global Time or Non-Determinism:**
   - Never call `std::chrono::steady_clock::now()`, `std::time()`, or `std::random_device` inside core state machines or generators.
   - Inject timestamps/deltas (`double now_ms`, `double dt_ms`, `std::int64_t epoch_day`) and seedable RNGs (`std::mt19937&` or explicit `uint32_t seed`) as function parameters so every algorithm is 100% deterministic in `Catch2` unit tests.
3. **No Hidden File I/O Inside Pure Engines:**
   - Theory, flashcard, etude, chord-matcher, SRS, ear-trainer, and autopilot state machines operate purely on in-memory structs.
   - Only `profile_store` handles JSON serialization/deserialization (and must expose string/JSON conversion functions testable without touching the filesystem).

## Testing Requirement

- Every public type and non-trivial function in `src/core/` must be developed test-first via the `cpp23-tdd` skill with `Catch2` unit tests in `tests/`.
