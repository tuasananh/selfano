#pragma once

#include "core/lesson_score.hpp"
#include "core/pitch.hpp"

#include <cstdint>

namespace selfano::ui {

// Bravura.otf head.unitsPerEm = 1000, hhea.ascent - hhea.descent = 4024.
// Dear ImGui's stb_truetype loader scales by 1 / (ascent - descent), so multiply
// desired SMuFL em-height (4 * staff_space) by this factor.
inline constexpr float kBravuraEmToPixelHeightScale = 4024.0f / 1000.0f;

// Bravura.json engravingDefaults (in staff spaces)
inline constexpr float kStaffLineThicknessSpaces = 0.13f;
inline constexpr float kStemThicknessSpaces = 0.12f;
inline constexpr float kBeamThicknessSpaces = 0.50f;
inline constexpr float kBeamSpacingSpaces = 0.25f;
inline constexpr float kLedgerLineThicknessSpaces = 0.16f;
inline constexpr float kLedgerLineExtensionSpaces = 0.40f;
inline constexpr float kThinBarlineThicknessSpaces = 0.16f;
inline constexpr float kThickBarlineThicknessSpaces = 0.50f;

// Bravura glyph widths & stem anchors (in staff spaces)
inline constexpr float kNoteheadBlackWidthSpaces = 1.18f;
inline constexpr float kNoteheadHalfWidthSpaces = 1.18f;
inline constexpr float kNoteheadWholeWidthSpaces = 1.688f;
inline constexpr float kStemUpAnchorXSpaces = 1.18f;
inline constexpr float kStemAnchorOffsetYSpaces = 0.168f;
inline constexpr float kGClefWidthSpaces = 2.684f;
inline constexpr float kFClefWidthSpaces = 2.736f;
inline constexpr float kBraceHeightAt1EmSpaces = 4.00244f;

// SMuFL UTF-8 glyph codepoint literals
inline constexpr const char* kGlyphBrace = "\xEE\x80\x80";                 // U+E000
inline constexpr const char* kGlyphGClef = "\xEE\x81\x90";                 // U+E050
inline constexpr const char* kGlyphFClef = "\xEE\x81\xA2";                 // U+E062
inline constexpr const char* kGlyphTimeSig0 = "\xEE\x82\x80";              // U+E080
inline constexpr const char* kGlyphTimeSig1 = "\xEE\x82\x81";              // U+E081
inline constexpr const char* kGlyphTimeSig2 = "\xEE\x82\x82";              // U+E082
inline constexpr const char* kGlyphTimeSig3 = "\xEE\x82\x83";              // U+E083
inline constexpr const char* kGlyphTimeSig4 = "\xEE\x82\x84";              // U+E084
inline constexpr const char* kGlyphTimeSig5 = "\xEE\x82\x85";              // U+E085
inline constexpr const char* kGlyphTimeSig6 = "\xEE\x82\x86";              // U+E086
inline constexpr const char* kGlyphTimeSig7 = "\xEE\x82\x87";              // U+E087
inline constexpr const char* kGlyphTimeSig8 = "\xEE\x82\x88";              // U+E088
inline constexpr const char* kGlyphTimeSig9 = "\xEE\x82\x89";              // U+E089
inline constexpr const char* kGlyphNoteheadWhole = "\xEE\x82\xA2";         // U+E0A2
inline constexpr const char* kGlyphNoteheadHalf = "\xEE\x82\xA3";          // U+E0A3
inline constexpr const char* kGlyphNoteheadBlack = "\xEE\x82\xA4";         // U+E0A4
inline constexpr const char* kGlyphAugmentationDot = "\xEE\x87\xA7";       // U+E1E7
inline constexpr const char* kGlyphFlag8thUp = "\xEE\x89\x80";             // U+E240
inline constexpr const char* kGlyphFlag8thDown = "\xEE\x89\x81";           // U+E241
inline constexpr const char* kGlyphFlag16thUp = "\xEE\x89\x82";            // U+E242
inline constexpr const char* kGlyphFlag16thDown = "\xEE\x89\x83";          // U+E243
inline constexpr const char* kGlyphFlag32ndUp = "\xEE\x89\x84";            // U+E244
inline constexpr const char* kGlyphFlag32ndDown = "\xEE\x89\x85";          // U+E245
inline constexpr const char* kGlyphAccidentalFlat = "\xEE\x89\xA0";        // U+E260
inline constexpr const char* kGlyphAccidentalNatural = "\xEE\x89\xA1";     // U+E261
inline constexpr const char* kGlyphAccidentalSharp = "\xEE\x89\xA2";       // U+E262
inline constexpr const char* kGlyphAccidentalDoubleSharp = "\xEE\x89\xA3"; // U+E263
inline constexpr const char* kGlyphAccidentalDoubleFlat = "\xEE\x89\xA4";  // U+E264
inline constexpr const char* kGlyphRestWhole = "\xEE\x93\xA3";             // U+E4E3
inline constexpr const char* kGlyphRestHalf = "\xEE\x93\xA4";              // U+E4E4
inline constexpr const char* kGlyphRestQuarter = "\xEE\x93\xA5";           // U+E4E5
inline constexpr const char* kGlyphRest8th = "\xEE\x93\xA6";               // U+E4E6
inline constexpr const char* kGlyphRest16th = "\xEE\x93\xA7";              // U+E4E7
inline constexpr const char* kGlyphRest32nd = "\xEE\x93\xA8";              // U+E4E8
inline constexpr const char* kGlyphArticAccentAbove = "\xEE\x92\xA0";      // U+E4A0
inline constexpr const char* kGlyphArticAccentBelow = "\xEE\x92\xA1";      // U+E4A1
inline constexpr const char* kGlyphArticStaccatoAbove = "\xEE\x92\xA2";    // U+E4A2
inline constexpr const char* kGlyphArticStaccatoBelow = "\xEE\x92\xA3";    // U+E4A3
inline constexpr const char* kGlyphArticTenutoAbove = "\xEE\x92\xA4";      // U+E4A4
inline constexpr const char* kGlyphArticTenutoBelow = "\xEE\x92\xA5";      // U+E4A5
inline constexpr const char* kGlyphDynamicP = "\xEE\x94\xA0";              // U+E520
inline constexpr const char* kGlyphDynamicM = "\xEE\x94\xA1";              // U+E521
inline constexpr const char* kGlyphDynamicF = "\xEE\x94\xA2";              // U+E522
inline constexpr const char* kGlyphDynamicPP = "\xEE\x94\xAB";             // U+E52B
inline constexpr const char* kGlyphDynamicMP = "\xEE\x94\xAC";             // U+E52C
inline constexpr const char* kGlyphDynamicMF = "\xEE\x94\xAD";             // U+E52D
inline constexpr const char* kGlyphDynamicFF = "\xEE\x94\xAF";             // U+E52F
inline constexpr const char* kGlyphPedalPed = "\xEE\x99\x90";              // U+E650
inline constexpr const char* kGlyphPedalRelease = "\xEE\x99\x95";          // U+E655
inline constexpr const char* kGlyphWiggleArpeggiatoUp = "\xEE\xAA\xA9";    // U+EAA9

[[nodiscard]] constexpr const char* dynamic_level_glyph(core::DynamicLevel level) noexcept {
  switch (level) {
  case core::DynamicLevel::PP:
    return kGlyphDynamicPP;
  case core::DynamicLevel::P:
    return kGlyphDynamicP;
  case core::DynamicLevel::MP:
    return kGlyphDynamicMP;
  case core::DynamicLevel::MF:
    return kGlyphDynamicMF;
  case core::DynamicLevel::F:
    return kGlyphDynamicF;
  case core::DynamicLevel::FF:
    return kGlyphDynamicFF;
  }
  return kGlyphDynamicMF;
}

[[nodiscard]] constexpr const char* time_sig_digit_glyph(std::uint8_t digit) noexcept {
  switch (digit) {
  case 0:
    return kGlyphTimeSig0;
  case 1:
    return kGlyphTimeSig1;
  case 2:
    return kGlyphTimeSig2;
  case 3:
    return kGlyphTimeSig3;
  case 4:
    return kGlyphTimeSig4;
  case 5:
    return kGlyphTimeSig5;
  case 6:
    return kGlyphTimeSig6;
  case 7:
    return kGlyphTimeSig7;
  case 8:
    return kGlyphTimeSig8;
  case 9:
    return kGlyphTimeSig9;
  default:
    return kGlyphTimeSig4;
  }
}

[[nodiscard]] constexpr const char* accidental_glyph(core::Accidental accidental) noexcept {
  switch (accidental) {
  case core::Accidental::DoubleFlat:
    return kGlyphAccidentalDoubleFlat;
  case core::Accidental::Flat:
    return kGlyphAccidentalFlat;
  case core::Accidental::Natural:
    return kGlyphAccidentalNatural;
  case core::Accidental::Sharp:
    return kGlyphAccidentalSharp;
  case core::Accidental::DoubleSharp:
    return kGlyphAccidentalDoubleSharp;
  }
  return kGlyphAccidentalNatural;
}

[[nodiscard]] constexpr float accidental_width_spaces(core::Accidental accidental) noexcept {
  switch (accidental) {
  case core::Accidental::DoubleFlat:
    return 1.644f;
  case core::Accidental::Flat:
    return 0.904f;
  case core::Accidental::Natural:
    return 0.672f;
  case core::Accidental::Sharp:
    return 0.996f;
  case core::Accidental::DoubleSharp:
    return 0.988f;
  }
  return 0.996f;
}

} // namespace selfano::ui
