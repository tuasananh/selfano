---
name: verification-and-review
description: >-
  Goal-backward verification and pre-completion code review checklist. Use
  before claiming any task, vertical slice, or roadmap phase is complete, or
  when reviewing a git diff against AGENTS.md and the MVP spec.
---

# Verification Before Completion & Code Review

Synthesized from Superpowers (`verification-before-completion`, `requesting-code-review`), GSD (goal-backward verification), and Matt Pocock (`code-review`).

## Iron Law

**Never claim a build passes, tests pass, or a phase is complete without fresh command output executed AFTER the final edit.**

## Step 1 — Goal-Backward Spec Audit

Open the active phase packet (`docs/phases/phase-N-*.md`) and trace **backward** from every listed deliverable and exit gate:
- Does the exact behavior exist in code (not stubbed or TODO'd)?
- Is every core invariant backed by a `Catch2` unit test?
- Are all edge cases from the MVP spec handled (e.g., `velocity == 0` as NoteOff, 2nd-interval notehead collision offset, audio fallback when no `MIDI OUT` exists)?

## Step 2 — Automated Verification Suite

Run in one batched shell check after the final edit:

```bash
# 1. Clean warning-free build & unit tests
cmake --build --preset native-debug && \
ctest --preset native-debug --output-on-failure && \
# 2. Enforce piano_core purity boundary (expect 0 matches)
! grep -rnE '#include\s*[<"](imgui|SDL|libremidi|emscripten)' src/core/ && \
# 3. Inspect git status and diff stats
git status --short && git diff --stat
```

For Phase 6 (or any changes touching `CMakeLists.txt`, `src/main.cpp`, `src/midi/`, or persistence), also verify:
```bash
emcmake cmake --preset web-release && cmake --build --preset web-release
```

## Step 3 — Focused Diff Review Checklist

Inspect `git diff` against Selfano's guardrails:
- [ ] **Architecture:** Zero UI/SDL/MIDI headers in `src/core/`; no mutexes, allocations, or UI calls inside MIDI/audio callbacks.
- [ ] **Modern C++23:** Zero raw owning `new`/`delete`; proper `std::expected` error handling; no uninitialized variables or unchecked narrowing.
- [ ] **Determinism:** No hidden `steady_clock::now()` or unseeded randomness inside `piano_core`.
- [ ] **Formatting & Hygiene:** `clang-format` clean, no leftover debug prints or stray temp files.
- [ ] **State Sync:** `docs/STATE.md` and `docs/ROADMAP.md` reflect the verified result.

## Step 4 — Evidence Report

When reporting completion to the user, cite:
- Exact commands run and pass/fail counts (`ctest` assertions/test cases).
- Any manual verification performed (e.g., native window launch, MIDI/QWERTY interaction, `chrome-devtools` WebGUI check) or clearly state what still requires user hardware testing (e.g., physical Korg LP-380 DIN cable).
