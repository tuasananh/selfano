#pragma once

#include "core/pitch.hpp"

namespace selfano::ui {

// Bravura.otf head.unitsPerEm = 1000, hhea.ascent - hhea.descent = 4024.
// Dear ImGui's stb_truetype loader scales by 1 / (ascent - descent), so multiply
// desired SMuFL em-height (4 * staff_space) by this factor.
inline constexpr float kBravuraEmToPixelHeightScale = 4024.0f / 1000.0f;

// Bravura.json engravingDefaults (in staff spaces)
inline constexpr float kStaffLineThicknessSpaces = 0.13f;
inline constexpr float kStemThicknessSpaces = 0.12f;
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
inline constexpr const char* kGlyphNoteheadWhole = "\xEE\x82\xA2";         // U+E0A2
inline constexpr const char* kGlyphNoteheadHalf = "\xEE\x82\xA3";          // U+E0A3
inline constexpr const char* kGlyphNoteheadBlack = "\xEE\x82\xA4";         // U+E0A4
inline constexpr const char* kGlyphAccidentalFlat = "\xEE\x89\xA0";        // U+E260
inline constexpr const char* kGlyphAccidentalNatural = "\xEE\x89\xA1";     // U+E261
inline constexpr const char* kGlyphAccidentalSharp = "\xEE\x89\xA2";       // U+E262
inline constexpr const char* kGlyphAccidentalDoubleSharp = "\xEE\x89\xA3"; // U+E263
inline constexpr const char* kGlyphAccidentalDoubleFlat = "\xEE\x89\xA4";  // U+E264
inline constexpr const char* kGlyphRestQuarter = "\xEE\x93\xA5";           // U+E4E5

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
